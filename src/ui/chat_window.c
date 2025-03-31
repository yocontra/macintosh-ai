#include <Controls.h>
#include <Fonts.h>
#include <Memory.h>
#include <Quickdraw.h>
#include <TextEdit.h>
#include <TextUtils.h>
#include <Windows.h>
#include <stdio.h>
#include <string.h>

#include "../chatbot/model_manager.h"
#include "../constants.h"
#include "../error.h"
#include "chat_window.h"

/* Private module variables */
static WindowRef sWindow    = NULL;
static Boolean sInitialized = false;
static Boolean sIsVisible   = false;
static Rect sContentRect;                    /* Main content area */
static Rect sInputRect;                      /* Input field at bottom */
static Rect sDisplayRect;                    /* Display area for conversation */
static TEHandle sInputTE        = NULL;      /* Text edit handle for input */
static TEHandle sDisplayTE      = NULL;      /* Text edit handle for chat display */
static ControlHandle sScrollBar = NULL;      /* Scrollbar for chat display */
static char sPromptBuffer[kMaxPromptLength]; /* Buffer for user prompt */

/* AI model type - change this to switch between models */
static AIModelType sActiveModel = kMarkovModel; /* Default to Markov */

/* Private function declarations */
static void _UpdateChatScrollbar(void);
static void DrawChatInput(void);
static void ClearChatInput(void);
static void FormatAndAddMessage(const char *message, Boolean isUserMessage);
static void RefreshConversationDisplay(void);
static pascal void ScrollAction(ControlHandle control, short part);

/* Initialize and create the chat window */
void ChatWindow_Initialize(void)
{
    Rect windowRect;
    short screenWidth, screenHeight;
    short windowWidth, windowHeight;
    OSErr err;

    /* Prevent double initialization */
    if (sInitialized) {
        return;
    }

    /* Initialize the AI models here (using our selected model) */
    /* Set the active AI model */
    SetActiveAIModel(sActiveModel);

    /* Initialize the AI models */
    InitModels();

    /* Add welcome message that will appear when the window is shown */
    char welcomeMsg[200];
    sprintf(welcomeMsg, "AI initialized! Using %s model. How can I help you today?",
            (sActiveModel == kMarkovModel) ? "Markov chain (local)" : "OpenAI (remote)");

    /* Add message to conversation history */
    AddAIResponse(welcomeMsg);

    /* Check available memory first */
    err = CheckMemory();
    if (err != noErr) {
        HandleError(kErrMemoryFull, kCtxOpeningChatWindow, false); /* Non-fatal */
        return;
    }

    /* Calculate a window size that fits well on the Macintosh Plus screen */
    screenWidth  = qd.screenBits.bounds.right - qd.screenBits.bounds.left;
    screenHeight = qd.screenBits.bounds.bottom - qd.screenBits.bounds.top;

    /* Make sure the chat window is not too big (max 80% of screen) */
    windowWidth  = (screenWidth * 4 / 5);
    windowHeight = screenHeight - 80;

    SetRect(&windowRect, (screenWidth - windowWidth) / 2, /* Center horizontally */
            40,                                           /* Top margin */
            (screenWidth - windowWidth) / 2 + windowWidth, 40 + windowHeight); /* Bottom margin */

    /* Create the window - using direct creation for more control */
    sWindow = NewWindow(NULL, &windowRect, "\pAI Chat", true, documentProc, (WindowPtr)-1, true, 0);

    if (sWindow == NULL) {
        HandleError(kErrWindowCreation, kCtxOpeningChatWindow, false); /* Non-fatal */
        return;
    }

    SetPort(sWindow);

    /* Setup main content area (whole window) */
    sContentRect = sWindow->portRect;

    /* Setup input area at bottom */
    SetRect(&sInputRect, kPromptMargin, sContentRect.bottom - kChatInputHeight - kPromptMargin,
            sContentRect.right - kPromptMargin, sContentRect.bottom - kPromptMargin);

    /* Create a vertical scroll bar for the chat display - width is 16 pixels */
    Rect scrollBarRect;
    /* Adjust to match the display area exactly for proper scrolling */
    SetRect(&scrollBarRect, sContentRect.right - kResponseMargin - 16, kResponseMargin,
            sContentRect.right - kResponseMargin, sInputRect.top - kResponseMargin - 10);

    /* Setup display area - adjust right edge to leave room for scrollbar and bottom to avoid
     * overlapping prompt text */
    SetRect(&sDisplayRect, kResponseMargin, kResponseMargin,
            sContentRect.right - kResponseMargin - 16, sInputRect.top - kResponseMargin - 10);

    /* Create a TextEdit record for the chat display with proper margins */
    Rect viewRect = sDisplayRect;
    Rect destRect = sDisplayRect;

    /* Add small inset to create margin */
    InsetRect(&viewRect, 4, 4);
    InsetRect(&destRect, 4, 4);

    /* Create with adjusted rects */
    sDisplayTE = TENew(&destRect, &viewRect);
    if (sDisplayTE == NULL) {
        /* Failed to create text edit field */
        HandleError(kErrTextEditCreation, kCtxCreatingResponseArea, false); /* Non-fatal */
        DisposeWindow(sWindow);
        sWindow = NULL;
        return;
    }

    /* Note: Classic TextEdit doesn't have the concept of "read-only" */
    /* but we don't need to process keystrokes for it since it's not active */
    TEFeatureFlag(teFAutoScroll, teBitSet, sDisplayTE);

    /* Create the scroll bar control with proper Mac scrollbar style */
    sScrollBar = NewControl(sWindow, &scrollBarRect, "\p", /* No title for scroll bar */
                            true,                          /* Visible */
                            0,                             /* Initial value */
                            0,                             /* Min value */
                            0,                 /* Max value - will update as content grows */
                            scrollBarProc,     /* Standard Mac scrollbar proc (16) */
                            (long)sDisplayTE); /* Store TextEdit handle as refCon */

    if (sScrollBar == NULL) {
        /* Failed to create scrollbar but can continue without it */
        HandleError(kErrControlCreation, kCtxCreatingResponseArea, false); /* Non-fatal */
    }

    /* Create chat input text edit field */
    sInputTE = TENew(&sInputRect, &sInputRect);
    if (sInputTE == NULL) {
        /* Failed to create text edit field */
        HandleError(kErrTextEditCreation, kCtxCreatingPromptArea, false); /* Non-fatal */
        TEDispose(sDisplayTE);
        if (sScrollBar != NULL) {
            DisposeControl(sScrollBar);
        }
        DisposeWindow(sWindow);
        sWindow    = NULL;
        sDisplayTE = NULL;
        sScrollBar = NULL;
        return;
    }

    /* Make the input field active and set properties */
    TEActivate(sInputTE);
    TEAutoView(true, sInputTE); /* Enable auto-scrolling */

    /* Set proper text attributes */
    TextFont(kFontGeneva);
    TextSize(12);
    TextFace(normal);
    ForeColor(blackColor);

    /* Make the text field editable with cursor properly positioned */
    TESetSelect(0, 0, sInputTE);
    TEFeatureFlag(teFAutoScroll, teBitSet, sInputTE);

    /* Initialize the chat display - empty at first - call static function */
    _UpdateChatScrollbar();

    sInitialized = true;
    sIsVisible   = false; /* Window is created but not shown yet */
}

/* Dispose of the chat window and clean up resources */
void ChatWindow_Dispose(void)
{
    if (!sInitialized) {
        return;
    }

    /* Explicitly dispose of resources BEFORE closing the window */
    /* First the text edit controls */
    if (sInputTE != NULL) {
        TEDeactivate(sInputTE);
        TEDispose(sInputTE);
        sInputTE = NULL;
    }

    if (sDisplayTE != NULL) {
        TEDeactivate(sDisplayTE);
        TEDispose(sDisplayTE);
        sDisplayTE = NULL;
    }

    /* Then the scrollbar control */
    if (sScrollBar != NULL) {
        DisposeControl(sScrollBar);
        sScrollBar = NULL;
    }

    /* Now dispose the window after all resources are freed */
    if (sWindow != NULL) {
        DisposeWindow(sWindow);
        sWindow = NULL;
    }

    sInitialized = false;
    sIsVisible   = false;
}

/* Function to toggle between AI models */
void ChatWindow_ToggleAIModel(void)
{
    /* Toggle the model */
    if (sActiveModel == kMarkovModel) {
        sActiveModel = kOpenAIModel;
    }
    else {
        sActiveModel = kMarkovModel;
    }

    /* Update the model selection in the AI interface */
    SetActiveAIModel(sActiveModel);

    /* Inform the user about the model change */
    char modelMsg[100];
    sprintf(modelMsg, "Switched to %s model.",
            (sActiveModel == kMarkovModel) ? "Markov chain (local)" : "OpenAI (remote)");

    /* Add message to chat window */
    FormatAndAddMessage(modelMsg, false);
}

/* Update the chat scrollbar based on current text content */
/* Scrollbar action procedure - handle continuous scrolling */
static pascal void ScrollAction(ControlHandle control, short part)
{
    short value, min, max, delta;
    short oldValue;

    if (control == NULL || sDisplayTE == NULL || *sDisplayTE == NULL)
        return;

    /* Get current control values */
    oldValue = GetControlValue(control);
    value    = oldValue;
    min      = GetControlMinimum(control);
    max      = GetControlMaximum(control);

    /* Process based on which part was clicked */
    switch (part) {
    case inUpButton: /* Up arrow */
        delta = -16; /* Scroll up about one line */
        value += delta;
        break;

    case inDownButton: /* Down arrow */
        delta = 16;    /* Scroll down about one line */
        value += delta;
        break;

    case inPageUp: /* Page up (above thumb) */
        delta = -((*sDisplayTE)->viewRect.bottom - (*sDisplayTE)->viewRect.top) * 2 / 3;
        value += delta;
        break;

    case inPageDown: /* Page down (below thumb) */
        delta = ((*sDisplayTE)->viewRect.bottom - (*sDisplayTE)->viewRect.top) * 2 / 3;
        value += delta;
        break;

    case inThumb:
        /* For thumb drags, get the new position directly from the control */
        value = GetControlValue(control);
        break;

    default:
        return; /* Unknown part */
    }

    /* Ensure value stays within bounds */
    if (value < min)
        value = min;
    if (value > max)
        value = max;

    /* Only proceed if the value actually changed */
    if (value != oldValue) {
        /* Update control value */
        SetControlValue(control, value);

        /* For safer scrolling, we'll always set the absolute position
           rather than using relative scrolling */
        TEScroll(0, 0, sDisplayTE);      /* First reset to top */
        TEScroll(0, -value, sDisplayTE); /* Then scroll to position */

        /* Force redraw of the text area */
        InvalRect(&sDisplayRect);
    }
}

/* Update the scrollbar based on current text content */
static void _UpdateChatScrollbar(void)
{
    /* Enhanced safety checks for TextEdit and ScrollBar */
    if (sScrollBar == NULL || sDisplayTE == NULL || *sDisplayTE == NULL)
        return;

    /* Calculate total scrollable amount based on text height */
    short viewHeight = (*sDisplayTE)->viewRect.bottom - (*sDisplayTE)->viewRect.top;

    /* Get the height of all text */
    short textHeight = TEGetHeight(0, (*sDisplayTE)->teLength, sDisplayTE);

    /* Add extra padding to ensure first line is fully visible */
    textHeight += 8;

    if (textHeight <= viewHeight) {
        /* Content fits in view, disable scrollbar */
        HiliteControl(sScrollBar, 255); /* 255 = disabled */
        SetControlMaximum(sScrollBar, 0);
        SetControlValue(sScrollBar, 0);

        /* Reset text view to top */
        TEScroll(0, 0, sDisplayTE);
    }
    else {
        short scrollPos;
        short maxScroll = textHeight - viewHeight;

        /* Content is larger than view, enable scrollbar */
        HiliteControl(sScrollBar, 0); /* 0 = enabled */

        /* Make sure maximum is a positive value */
        if (maxScroll < 0)
            maxScroll = 0;

        /* Set maximum value */
        SetControlMaximum(sScrollBar, maxScroll);

        /* Keep scroll position at the bottom to show newest messages */
        scrollPos = maxScroll;

        /* Update thumb position */
        SetControlValue(sScrollBar, scrollPos);

        /* Reset position and scroll to show latest messages */
        TEScroll(0, 0, sDisplayTE);          /* Reset to top */
        TEScroll(0, -scrollPos, sDisplayTE); /* Scroll to bottom */

        /* Invalidate to ensure proper redraw */
        InvalRect(&sDisplayRect);
    }
}

/* Draw the chat input field */
static void DrawChatInput(void)
{
    Rect frameRect  = sInputRect;
    Rect shadowRect = frameRect;
    Rect innerRect  = frameRect;

    /* Draw shadow effect (standard on Mac) */
    PenSize(1, 1);
    OffsetRect(&shadowRect, 1, 1);
    PenPat(&qd.gray);
    FrameRect(&shadowRect);
    PenNormal();

    /* Draw a frame around the input area */
    FrameRect(&frameRect);

    /* Draw a light background for the input field */
    InsetRect(&frameRect, 1, 1);
    BackPat(&qd.white);
    EraseRect(&frameRect);

    /* Adjust text positioning with proper inset for cursor */
    if (sInputTE != NULL) {
        /* Proper inset for text in Mac UI guidelines */
        innerRect = frameRect;
        InsetRect(&innerRect, 3, 3);
        (*sInputTE)->viewRect = innerRect;
        (*sInputTE)->destRect = innerRect;
        TECalText(sInputTE);
    }

    /* Draw prompt label */
    MoveTo(frameRect.left - frameRect.left + 5, frameRect.top - 6);
    TextFont(kFontGeneva);
    TextSize(9);
    TextFace(bold);
    DrawString("\pType your prompt and press Return (Shift+Return for new line):");
    TextFace(normal);
}

/* Handle mouse clicks in the content area of the chat window */
Boolean ChatWindow_HandleContentClick(Point localPt)
{
    if (!sInitialized || sWindow == NULL) {
        return false;
    }

    /* Handle clicks in the scrollbar - check this first as it has highest priority */
    if (sScrollBar != NULL) {
        ControlHandle clickedControl;
        short part;

        part = FindControl(localPt, sWindow, &clickedControl);

        /* Check if the click was in our scrollbar */
        if (part && clickedControl == sScrollBar) {
            /* Handle scrolling with appropriate action */
            if (part == inThumb) {
                /* For thumb dragging, we use a specific approach */
                short oldValue = GetControlValue(sScrollBar);

                /* Track without action proc for thumb */
                part = TrackControl(clickedControl, localPt, NULL);

                if (part) {
                    /* After tracking completes, get new position and scroll */
                    short newValue = GetControlValue(sScrollBar);

                    if (newValue != oldValue) {
                        /* Scroll to the new position */
                        TEScroll(0, 0, sDisplayTE);         /* Reset to top */
                        TEScroll(0, -newValue, sDisplayTE); /* Then to position */

                        /* Force redraw */
                        InvalRect(&sDisplayRect);
                    }
                }
            }
            else {
                /* For other parts, use our action proc */
                part = TrackControl(clickedControl, localPt, (ControlActionUPP)ScrollAction);
            }
            return true;
        }
    }

    /* Handle clicks in the chat display area */
    if (sDisplayTE != NULL && PtInRect(localPt, &sDisplayRect)) {
        /* Handle clicks in the text display - text selection */
        TEClick(localPt, false, sDisplayTE);

        /* Force redraw to show changes */
        InvalRect(&sDisplayRect);
        return true;
    }

    /* Check if the click is in the input field */
    if (sInputTE != NULL && PtInRect(localPt, &sInputRect)) {
        TEClick(localPt, false, sInputTE);
        return true;
    }

    return false; /* Click not handled */
}

/* Handle key events in the chat window */
Boolean ChatWindow_HandleKeyDown(char key, Boolean isShiftDown, Boolean isCmdDown)
{
    if (!sInitialized || sWindow == NULL || sInputTE == NULL) {
        return false;
    }

    /* Handle Return key */
    if (key == '\r' || key == '\n') {
        if (isShiftDown) {
            /* Shift-Return: Insert line break in input field */
            char newline = '\r';
            TEKey(newline, sInputTE);
            return true;
        }
        else {
            /* Regular Return: Submit prompt and generate response */
            ChatWindow_SendMessage();
            return true;
        }
    }
    else {
        /* Pass other key presses to the input field */
        TEKey(key, sInputTE);
        return true;
    }
}

/* Handle window activation/deactivation events */
void ChatWindow_HandleActivate(Boolean becomingActive)
{
    if (!sInitialized || sWindow == NULL) {
        return;
    }

    if (becomingActive) {
        /* Window being activated */
        if (sInputTE != NULL) {
            TEActivate(sInputTE);
        }
        if (sDisplayTE != NULL) {
            TEActivate(sDisplayTE);
        }
    }
    else {
        /* Window being deactivated */
        if (sInputTE != NULL) {
            TEDeactivate(sInputTE);
        }
        if (sDisplayTE != NULL) {
            TEDeactivate(sDisplayTE);
        }
    }
}

/* Handle all events for the chat window */
void ChatWindow_HandleEvent(EventRecord *event)
{
    if (event->what == keyDown) {
        char key            = event->message & charCodeMask;
        Boolean isShiftDown = (event->modifiers & shiftKey) != 0;
        Boolean isCmdDown   = (event->modifiers & cmdKey) != 0;
        ChatWindow_HandleKeyDown(key, isShiftDown, isCmdDown);
    }
    else if (event->what == mouseDown) {
        Point mousePt = event->where;
        GlobalToLocal(&mousePt);
        ChatWindow_HandleContentClick(mousePt);
    }
    else if (event->what == activateEvt) {
        Boolean becomingActive = (event->modifiers & activeFlag) != 0;
        ChatWindow_HandleActivate(becomingActive);
    }
}

/* Handle update events for the chat window */
void ChatWindow_Update(void)
{
    if (!sInitialized || sWindow == NULL) {
        return;
    }

    SetPort(sWindow);
    BeginUpdate(sWindow);

    /* Erase the window area with white background */
    BackColor(whiteColor);
    EraseRect(&sWindow->portRect);

    /* Draw the scrollbar if available */
    if (sScrollBar != NULL) {
        DrawControls(sWindow);
    }

    /* Draw the chat display area */
    if (sDisplayTE != NULL) {
        GrafPtr savePort;
        Rect clipSave;

        /* Draw a frame around the display area */
        PenNormal();
        PenSize(1, 1);
        FrameRect(&sDisplayRect);

        /* Save current drawing environment */
        GetPort(&savePort);
        clipSave = savePort->clipRgn[0]->rgnBBox;

        /* Create a slightly inset clip region to prevent text from touching the border */
        Rect textClip = sDisplayRect;
        InsetRect(&textClip, 3, 3);
        ClipRect(&textClip);

        /* Update the text content */
        TEUpdate(&(*sDisplayTE)->viewRect, sDisplayTE);

        /* Restore original clipping */
        ClipRect(&clipSave);
    }

    /* Draw frame for input area */
    DrawChatInput();

    /* Update input text field if available */
    if (sInputTE != NULL) {
        TEUpdate(&sInputRect, sInputTE);
    }

    EndUpdate(sWindow);
}

/* Clear the chat input field */
static void ClearChatInput(void)
{
    /* Additional safety checks */
    if (!sInitialized || sWindow == NULL || sInputTE == NULL) {
        return;
    }

    /* Simpler approach - just set the text to empty */
    TESetText("", 0, sInputTE);
    TESetSelect(0, 0, sInputTE);

    /* Update visually */
    TEUpdate(&sInputRect, sInputTE);
}

/* Format a new message with proper styling */
static void FormatAndAddMessage(const char *message, Boolean isUserMessage)
{
    char formattedMsg[kMaxPromptLength + 100]; /* Extra space for formatting */
    char lineBuffer[80];                       /* Buffer for a single line of the header */
    short msgLength;

    if (message == NULL || sDisplayTE == NULL)
        return;

    /* Safety check for message length */
    if (strlen(message) == 0 || strlen(message) >= kMaxPromptLength)
        return;

    /* Format the message with proper header and indentation using safer string operations */
    memset(formattedMsg, 0, sizeof(formattedMsg)); /* Clear entire buffer */

    /* Calculate safe buffer space for each component to ensure we don't overflow */
    size_t remainingSpace = sizeof(formattedMsg) - 1; /* -1 for null terminator */
    size_t headerSpace    = 25;                       /* Space for header - reduced */
    size_t footerSpace    = 15;                       /* Space for footer - reduced */
    size_t messageSpace   = remainingSpace - headerSpace - footerSpace;

    /* Truncate the message if it's too long for our buffer */
    size_t safeMessageLen = strlen(message);
    if (safeMessageLen > messageSpace) {
        safeMessageLen = messageSpace;
    }

    /* Add minimal spacing before messages - only one line break */
    strncat(formattedMsg, "\r", remainingSpace);
    remainingSpace -= 1;

    /* Create a more compact header */
    if (isUserMessage) {
        /* Simplified header */
        strncat(formattedMsg, "You: ", remainingSpace);
        remainingSpace -= strlen("You: ");

        /* Add truncated message safely */
        strncat(formattedMsg, message,
                (safeMessageLen < remainingSpace) ? safeMessageLen : remainingSpace);
        remainingSpace -= ((safeMessageLen < remainingSpace) ? safeMessageLen : remainingSpace);
    }
    else {
        /* Simplified header */
        strncat(formattedMsg, "AI: ", remainingSpace);
        remainingSpace -= strlen("AI: ");

        /* Add truncated message safely */
        strncat(formattedMsg, message,
                (safeMessageLen < remainingSpace) ? safeMessageLen : remainingSpace);
        remainingSpace -= ((safeMessageLen < remainingSpace) ? safeMessageLen : remainingSpace);
    }

    /* Add the formatted message to the TextEdit field */
    /* First save current selection */
    short selStart = (*sDisplayTE)->selStart;
    short selEnd   = (*sDisplayTE)->selEnd;

    /* Insert the new message at the end */
    msgLength = strlen(formattedMsg);
    TESetSelect(32767, 32767, sDisplayTE); /* Move to end */

    /* Set text style for the message */
    TextFont(kFontMonaco); /* Monaco is monospaced and works better for ASCII art */

    if (isUserMessage) {
        TextFace(bold);
        TextSize(10); /* Slightly smaller for better fit */
    }
    else {
        TextFace(normal);
        TextSize(10);
    }

    /* Insert the text */
    TEInsert(formattedMsg, msgLength, sDisplayTE);

    /* Restore original selection if it was valid */
    if (selStart < selEnd && selEnd <= (*sDisplayTE)->teLength - msgLength) {
        TESetSelect(selStart, selEnd, sDisplayTE);
    }
    else {
        /* Keep selection at end to ensure visibility */
        TESetSelect(32767, 32767, sDisplayTE);
    }

    /* Update the scrollbar to reflect new content */
    _UpdateChatScrollbar();

    /* Force window update to show changes immediately */
    if (sWindow != NULL) {
        InvalRect(&sDisplayRect);
    }
}

/* Send a message from the chat input field */
void ChatWindow_SendMessage(void)
{
    char promptBuffer[kMaxPromptLength];
    char *response;

    /* Additional safety checks to prevent crashes */
    if (!sInitialized || sWindow == NULL || sInputTE == NULL || sDisplayTE == NULL) {
        return;
    }

    /* Get the user's prompt from the input field */
    int promptLen = (*sInputTE)->teLength;
    if (promptLen <= 0) {
        return; /* Empty prompt */
    }

    /* Make sure we don't exceed buffer size */
    if (promptLen >= kMaxPromptLength) {
        promptLen = kMaxPromptLength - 1;
    }

    /* Zero out the buffer before copying to ensure proper null termination */
    memset(promptBuffer, 0, kMaxPromptLength);

    /* Copy the text from the input field with bounds checking */
    if ((*sInputTE)->hText != NULL && *(*sInputTE)->hText != NULL) {
        BlockMoveData(*(*sInputTE)->hText, promptBuffer, promptLen);
        promptBuffer[promptLen] = '\0';
    }
    else {
        return; /* Invalid TextEdit handle */
    }

    /* Clear the input field FIRST before generating response */
    ClearChatInput();

    /* Add user prompt to conversation history */
    AddUserPrompt(promptBuffer);

    /* Add the user message to display */
    FormatAndAddMessage(promptBuffer, true); /* true = user message */

    /* Add temporary "Thinking..." indicator */
    char thinkingMsg[80];
    sprintf(thinkingMsg, "Processing your query...");

    /* Add thinking message to display (but NOT to conversation history) */
    FormatAndAddMessage(thinkingMsg, false); /* false = AI message */

    /* Ensure the thinking message is visible by forcing a redraw */
    InvalRect(&sDisplayRect);

    /* Generate AI response using the selected model */
    response = GenerateAIResponse(&gConversationHistory);

    /* Add AI response to conversation history */
    AddAIResponse(response);

    /* Fully refresh the conversation display - this ensures all messages are shown
       and completely removes the temporary thinking message */
    RefreshConversationDisplay();

    /* Update the display if window is still valid */
    if (sWindow != NULL) {
        InvalRect(&sWindow->portRect);
    }
}

/* Get the window reference for the chat window */
WindowRef ChatWindow_GetWindowRef(void)
{
    return sWindow;
}

/* Get the display TextEdit handle for external operations */
TEHandle ChatWindow_GetDisplayTE(void)
{
    return sDisplayTE;
}

/* Get the input TextEdit handle for external operations */
TEHandle ChatWindow_GetInputTE(void)
{
    return sInputTE;
}

/* Refresh the conversation history display */
static void RefreshConversationDisplay(void)
{
    short i;

    /* Safety check */
    if (!sInitialized || sDisplayTE == NULL) {
        return;
    }

    /* Clear the text display first */
    TESetText("", 0, sDisplayTE);

    /* Display all messages from the conversation history */
    for (i = 0; i < gConversationHistory.count; i++) {
        ConversationMessage *msg = &gConversationHistory.messages[i];
        if (msg->text[0] != '\0') {
            FormatAndAddMessage(msg->text, msg->type == kUserMessage);
        }
    }

    /* Make sure we're scrolled to show the most recent messages */
    _UpdateChatScrollbar();
}

/* Show or hide the chat window */
void ChatWindow_Show(Boolean visible)
{
    if (!sInitialized || sWindow == NULL) {
        return;
    }

    if (visible) {
        /* Refresh conversation display when showing window */
        RefreshConversationDisplay();

        ShowWindow(sWindow);
        SelectWindow(sWindow);
        InvalRect(&sWindow->portRect);
        sIsVisible = true;
    }
    else {
        HideWindow(sWindow);
        sIsVisible = false;
    }
}

/* Determine if the window is visible */
Boolean ChatWindow_IsVisible(void)
{
    if (!sInitialized || sWindow == NULL) {
        return false;
    }

    return sIsVisible;
}

/* Add a message to the chat display */
void ChatWindow_AddMessage(const char *message, Boolean isUserMessage)
{
    if (!sInitialized || sWindow == NULL || message == NULL || strlen(message) == 0) {
        return;
    }

    FormatAndAddMessage(message, isUserMessage);
}

/* Perform idle processing (text cursor blinking, etc.) */
void ChatWindow_Idle(void)
{
    if (!sInitialized || sWindow == NULL || !sIsVisible) {
        return;
    }

    /* Standard idle processing */
    if (sInputTE != NULL) {
        TEIdle(sInputTE);
    }
}