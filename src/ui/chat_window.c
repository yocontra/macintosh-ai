#include <Controls.h>
#include <Fonts.h>
#include <Memory.h>
#include <Quickdraw.h>
#include <TextEdit.h>
#include <TextUtils.h>
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../chatbot/model_manager.h"
#include "../constants.h"
#include "../error.h"
#include "chat_window.h"
#include "utils.h"

/* Module variables */
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

/* AI model type */
static AIModelType sActiveModel = kTemplateModel; /* Default to Template-based model */

/* Private function declarations */
static void ChatWindow_Update(void);
static void ChatWindow_Render(void);
static void ChatWindow_HandleActivate(Boolean becomingActive);
static Boolean ChatWindow_HandleContentClick(Point localPt);
static Boolean ChatWindow_HandleKeyDown(char key, Boolean isShiftDown, Boolean isCmdDown);
static void FormatAndAddMessage(const char *message, Boolean isUserMessage);
static void RefreshConversationDisplay(void);
static void DrawChatInput(void);
static void ClearChatInput(void);
static pascal void ScrollAction(ControlHandle control, short part);

/* Initialize and create the chat window */
void ChatWindow_Initialize(void)
{
    Rect windowRect, viewRect, destRect, scrollBarRect;
    short screenWidth, screenHeight, windowWidth, windowHeight;
    OSErr err;

    /* Prevent double initialization */
    if (sInitialized) {
        return;
    }

    /* Initialize the AI models */
    SetActiveAIModel(sActiveModel);
    InitModels();

    /* Check available memory */
    err = CheckMemory();
    if (err != noErr) {
        HandleError(kErrMemoryFull, kCtxOpeningChatWindow, false); /* Non-fatal */
        return;
    }

    /* Calculate a window size that fits well on the screen */
    screenWidth  = qd.screenBits.bounds.right - qd.screenBits.bounds.left;
    screenHeight = qd.screenBits.bounds.bottom - qd.screenBits.bounds.top;
    windowWidth  = (screenWidth * 4 / 5);
    windowHeight = screenHeight - 80;

    /* Create centered window */
    SetRect(&windowRect, (screenWidth - windowWidth) / 2, 40,
            (screenWidth - windowWidth) / 2 + windowWidth, 40 + windowHeight);

    /* Create the window */
    sWindow = NewWindow(NULL, &windowRect, "\pAI Chat", true, documentProc, (WindowPtr)-1, true, 0);

    if (sWindow == NULL) {
        HandleError(kErrWindowCreation, kCtxOpeningChatWindow, false);
        return;
    }

    SetPort(sWindow);

    /* Setup main content area (whole window) */
    sContentRect = sWindow->portRect;

    /* Setup input area at bottom */
    SetRect(&sInputRect, kPromptMargin, sContentRect.bottom - kChatInputHeight - kPromptMargin,
            sContentRect.right - kPromptMargin, sContentRect.bottom - kPromptMargin);

    /* Setup scrollbar */
    SetRect(&scrollBarRect, sContentRect.right - kResponseMargin - 16, kResponseMargin,
            sContentRect.right - kResponseMargin, sInputRect.top - kResponseMargin - 15);

    /* Setup display area */
    SetRect(&sDisplayRect, kResponseMargin, kResponseMargin,
            sContentRect.right - kResponseMargin - 16, sInputRect.top - kResponseMargin - 15);

    /* Create the text display area */
    viewRect = sDisplayRect;
    destRect = sDisplayRect;
    InsetRect(&viewRect, 4, 4);
    InsetRect(&destRect, 4, 4);

    /* Create text display with shared utility function */
    sDisplayTE = CreateStandardTextField(&viewRect, &destRect, false);
    if (sDisplayTE == NULL) {
        HandleError(kErrTextEditCreation, kCtxCreatingResponseArea, false);
        DisposeWindow(sWindow);
        sWindow = NULL;
        return;
    }

    /* Create the scroll bar */
    sScrollBar =
        NewControl(sWindow, &scrollBarRect, "\p", true, 0, 0, 0, scrollBarProc, (long)sDisplayTE);

    if (sScrollBar == NULL) {
        HandleError(kErrControlCreation, kCtxCreatingResponseArea, false);
    }

    /* Create chat input text field */
    sInputTE = CreateStandardTextField(&sInputRect, &sInputRect, true);
    if (sInputTE == NULL) {
        HandleError(kErrTextEditCreation, kCtxCreatingPromptArea, false);
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

    /* Set standard text formatting */
    ApplyTextFormatting(kFontGeneva, 12, normal);

    /* Update scrollbar to reflect empty text and make sure it has the right state */
    UpdateTextScrollbar(sDisplayTE, sScrollBar, true);
    if (sScrollBar != NULL) {
        HiliteControl(sScrollBar, 255); /* Initially disabled until we have content */
    }

    sInitialized = true;
    sIsVisible   = false;
}

/* Dispose of the chat window and clean up resources */
void ChatWindow_Dispose(void)
{
    if (!sInitialized) {
        return;
    }

    /* Dispose resources in proper order */
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

    if (sScrollBar != NULL) {
        DisposeControl(sScrollBar);
        sScrollBar = NULL;
    }

    if (sWindow != NULL) {
        DisposeWindow(sWindow);
        sWindow = NULL;
    }

    sInitialized = false;
    sIsVisible   = false;
}

/* Scrollbar action procedure */
static pascal void ScrollAction(ControlHandle control, short part)
{
    short value, min, max, oldValue, lineHeight, viewHeight;

    if (control == NULL || sDisplayTE == NULL || *sDisplayTE == NULL)
        return;

    /* Get current values */
    oldValue = GetControlValue(control);
    value    = oldValue;
    min      = GetControlMinimum(control);
    max      = GetControlMaximum(control);

    if (max == 0)
        return;

    /* Get line height */
    lineHeight = 12;
    if ((*sDisplayTE)->teLength > 0) {
        lineHeight = TEGetHeight(0, 1, sDisplayTE);
        if (lineHeight < 10)
            lineHeight = 10;
    }

    /* View height for page scrolling */
    viewHeight = (*sDisplayTE)->viewRect.bottom - (*sDisplayTE)->viewRect.top;

    /* Process based on part */
    switch (part) {
    case inUpButton:
        value -= lineHeight;
        break;
    case inDownButton:
        value += lineHeight;
        break;
    case inPageUp:
        value -= viewHeight;
        break;
    case inPageDown:
        value += viewHeight;
        break;
    case inThumb:
        value = GetControlValue(control);
        break;
    default:
        return;
    }

    /* Keep value within bounds */
    if (value < min)
        value = min;
    if (value > max)
        value = max;

    /* Only update if value changed */
    if (value != oldValue) {
        SetControlValue(control, value);
        UpdateTextScrollbar(sDisplayTE, sScrollBar, false);
        InvalRect(&sDisplayRect);
    }
}

/* Draw the chat input field */
static void DrawChatInput(void)
{
    Rect frameRect  = sInputRect;
    Rect shadowRect = frameRect;
    Rect innerRect  = frameRect;
    Rect labelClip;
    GrafPtr savePort;
    Rect origClip;

    /* Save original clipping */
    GetPort(&savePort);
    origClip = savePort->clipRgn[0]->rgnBBox;

    /* Draw the label text above the input area */
    SetRect(&labelClip, frameRect.left, frameRect.top - 15, frameRect.right, frameRect.top - 2);

    ClipRect(&labelClip);
    BackColor(whiteColor);
    EraseRect(&labelClip);

    /* Draw the label with proper text settings */
    MoveTo(frameRect.left + 5, frameRect.top - 5);
    ApplyTextFormatting(kFontGeneva, 9, bold);
    DrawString("\pType your prompt and press Return (Shift+Return for new line):");
    TextFace(normal);

    /* Draw the input box area */
    Rect inputAreaClip = frameRect;
    inputAreaClip.left -= 5;
    inputAreaClip.right += 5;
    inputAreaClip.bottom += 5;
    inputAreaClip.top -= 1;
    ClipRect(&inputAreaClip);

    /* Draw shadow effect */
    PenSize(1, 1);
    OffsetRect(&shadowRect, 1, 1);
    PenPat(&qd.gray);
    FrameRect(&shadowRect);
    PenNormal();

    /* Draw a frame around the input area */
    ForeColor(blackColor);
    FrameRect(&frameRect);

    /* Draw a light background for the input field */
    InsetRect(&frameRect, 1, 1);
    BackPat(&qd.white);
    BackColor(whiteColor);
    EraseRect(&frameRect);

    /* Adjust text positioning */
    if (sInputTE != NULL) {
        innerRect = frameRect;
        InsetRect(&innerRect, 3, 3);

        if ((*sInputTE)->viewRect.top != innerRect.top ||
            (*sInputTE)->viewRect.left != innerRect.left ||
            (*sInputTE)->viewRect.bottom != innerRect.bottom ||
            (*sInputTE)->viewRect.right != innerRect.right) {

            (*sInputTE)->viewRect = innerRect;
            (*sInputTE)->destRect = innerRect;
            TECalText(sInputTE);
        }
    }

    /* Return to standard clipping */
    ClipRect(&origClip);
}

/* Handle mouse clicks in the content area of the chat window */
static Boolean ChatWindow_HandleContentClick(Point localPt)
{
    if (!sInitialized || sWindow == NULL) {
        return false;
    }

    /* Handle clicks in the scrollbar */
    if (sScrollBar != NULL) {
        ControlHandle clickedControl;
        short part;

        part = FindControl(localPt, sWindow, &clickedControl);

        /* Check if the click was in our scrollbar */
        if (part && clickedControl == sScrollBar) {
            /* Make sure scrollbar is active before handling clicks */
            if (GetControlMaximum(sScrollBar) > 0) {
                if (part == inThumb) {
                    /* Track thumb without action proc */
                    short oldValue = GetControlValue(sScrollBar);
                    part           = TrackControl(clickedControl, localPt, NULL);
                    if (part) {
                        short newValue = GetControlValue(sScrollBar);
                        if (newValue != oldValue) {
                            UpdateTextScrollbar(sDisplayTE, sScrollBar, false);
                            InvalRect(&sDisplayRect);
                        }
                    }
                }
                else {
                    /* For other parts, use action proc */
                    part = TrackControl(clickedControl, localPt, (ControlActionUPP)ScrollAction);
                }
            }
            return true;
        }
    }

    /* Handle clicks in the chat display area */
    if (sDisplayTE != NULL && PtInRect(localPt, &sDisplayRect)) {
        GrafPtr savePort;
        Rect saveClip;

        GetPort(&savePort);
        saveClip = savePort->clipRgn[0]->rgnBBox;
        ClipRect(&sDisplayRect);

        short oldSelStart = (*sDisplayTE)->selStart;
        short oldSelEnd   = (*sDisplayTE)->selEnd;

        TEActivate(sDisplayTE);
        TEClick(localPt, false, sDisplayTE);

        Boolean selectionChanged =
            (oldSelStart != (*sDisplayTE)->selStart || oldSelEnd != (*sDisplayTE)->selEnd);

        TEDeactivate(sDisplayTE);

        if (sInputTE != NULL) {
            TEActivate(sInputTE);
        }

        ClipRect(&saveClip);

        if (selectionChanged) {
            InvalRect(&sDisplayRect);
        }

        return true;
    }

    /* Handle clicks in the input field */
    if (sInputTE != NULL && PtInRect(localPt, &sInputRect)) {
        GrafPtr savePort;
        Rect saveClip;

        GetPort(&savePort);
        saveClip = savePort->clipRgn[0]->rgnBBox;
        ClipRect(&sInputRect);

        short oldSelStart = (*sInputTE)->selStart;
        short oldSelEnd   = (*sInputTE)->selEnd;

        TEActivate(sInputTE);
        TEClick(localPt, false, sInputTE);

        if (oldSelStart != (*sInputTE)->selStart || oldSelEnd != (*sInputTE)->selEnd) {
            TEUpdate(&sInputRect, sInputTE);
        }

        ClipRect(&saveClip);
        return true;
    }

    return false;
}

/* Handle key events in the chat window */
static Boolean ChatWindow_HandleKeyDown(char key, Boolean isShiftDown, Boolean isCmdDown)
{
    if (!sInitialized || sWindow == NULL || sInputTE == NULL) {
        return false;
    }

    /* Handle command key shortcuts */
    if (isCmdDown) {
        switch (key) {
        case 'x': /* Cmd-X: Cut */
        case 'X':
            ChatWindow_CutText();
            return true;

        case 'c': /* Cmd-C: Copy */
        case 'C':
            ChatWindow_CopyText();
            return true;

        case 'v': /* Cmd-V: Paste */
        case 'V':
            ChatWindow_PasteText();
            return true;

        case 'a': /* Cmd-A: Select All */
        case 'A':
            if (sInputTE != NULL) {
                TESetSelect(0, (*sInputTE)->teLength, sInputTE);
                return true;
            }
            break;

        case '.' /* Cmd-Period: Delete/Clear text */:
            ChatWindow_ClearText();
            return true;
        }
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
            /* Regular Return: Submit prompt */
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
static void ChatWindow_HandleActivate(Boolean becomingActive)
{
    if (!sInitialized || sWindow == NULL) {
        return;
    }

    if (becomingActive) {
        /* Window being activated */
        if (sInputTE != NULL) {
            TEActivate(sInputTE);
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

    /* Force window redraw */
    InvalRect(&sWindow->portRect);
}

/* Render the chat window contents */
static void ChatWindow_Render(void)
{
    if (!sInitialized || sWindow == NULL) {
        return;
    }

    SetPort(sWindow);

    /* Draw the window with standard frame */
    DrawStandardFrame(sWindow);

    /* Draw a frame around the display area */
    if (sDisplayTE != NULL) {
        PenNormal();
        PenSize(1, 1);
        FrameRect(&sDisplayRect);
    }

    /* Draw the scrollbar */
    if (sScrollBar != NULL) {
        /* Update scrollbar state */
        short viewHeight = 0, textHeight = 0;
        if (sDisplayTE != NULL && *sDisplayTE != NULL) {
            viewHeight = (*sDisplayTE)->viewRect.bottom - (*sDisplayTE)->viewRect.top;
            textHeight = TEGetHeight(0, (*sDisplayTE)->teLength, sDisplayTE);

            /* Enable or disable scrollbar based on content */
            if (textHeight > viewHeight) {
                HiliteControl(sScrollBar, 0); /* enabled */
            }
            else {
                HiliteControl(sScrollBar, 255); /* disabled */
            }
        }

        DrawControls(sWindow);
    }

    /* Draw the chat display content */
    if (sDisplayTE != NULL) {
        GrafPtr savePort;
        Rect clipSave;

        GetPort(&savePort);
        clipSave = savePort->clipRgn[0]->rgnBBox;

        Rect textClip = sDisplayRect;
        InsetRect(&textClip, 3, 3);

        BackColor(whiteColor);
        ForeColor(blackColor);

        ClipRect(&textClip);
        TEUpdate(&(*sDisplayTE)->viewRect, sDisplayTE);

        ClipRect(&clipSave);
    }

    /* Draw the input area */
    DrawChatInput();

    /* Update input text field */
    if (sInputTE != NULL) {
        TEUpdate(&sInputRect, sInputTE);
    }
}

/* Handle update events for the chat window */
static void ChatWindow_Update(void)
{
    if (!sInitialized || sWindow == NULL) {
        return;
    }

    SetPort(sWindow);
    BeginUpdate(sWindow);

    /* Call the render function to draw window contents */
    ChatWindow_Render();

    EndUpdate(sWindow);
}

/* Clear the chat input field */
static void ClearChatInput(void)
{
    if (!sInitialized || sWindow == NULL || sInputTE == NULL) {
        return;
    }

    GrafPtr savePort;
    Rect saveClip;

    GetPort(&savePort);
    saveClip = savePort->clipRgn[0]->rgnBBox;

    Rect frameRect = sInputRect;
    InsetRect(&frameRect, 1, 1);
    ClipRect(&frameRect);

    BackColor(whiteColor);
    EraseRect(&frameRect);

    TESetSelect(0, 32767, sInputTE);
    TEDelete(sInputTE);
    TESetSelect(0, 0, sInputTE);

    TEActivate(sInputTE);
    TEUpdate(&frameRect, sInputTE);

    ClipRect(&saveClip);
}

/* Format a new message with proper styling */
static void FormatAndAddMessage(const char *message, Boolean isUserMessage)
{
    char formattedMsg[kMaxPromptLength + 100]; /* Extra space for formatting */
    short msgLength;
    static Boolean isAdding = false;

    /* Prevent recursive calls */
    if (isAdding || message == NULL || sDisplayTE == NULL || *sDisplayTE == NULL) {
        return;
    }

    isAdding = true;

    /* Safety check for message length */
    if (strlen(message) == 0 || strlen(message) >= kMaxPromptLength) {
        isAdding = false;
        return;
    }

    /* Check for auto-scroll */
    Boolean scrollToBottom = true;
    if (sScrollBar != NULL) {
        short maxScroll    = GetControlMaximum(sScrollBar);
        short oldScrollPos = GetControlValue(sScrollBar);
        scrollToBottom     = (oldScrollPos >= maxScroll - 15);
    }

    /* Format the message */
    strcpy(formattedMsg, isUserMessage ? "You: " : "AI: ");
    strcat(formattedMsg, message);
    strcat(formattedMsg, "\r");

    /* Apply text formatting */
    TextFont(kFontMonaco);
    TextSize(10);
    TextFace(isUserMessage ? bold : normal);

    /* Use clipping to reduce flicker */
    GrafPtr savePort;
    Rect saveClip;
    GetPort(&savePort);
    saveClip = savePort->clipRgn[0]->rgnBBox;
    ClipRect(&sDisplayRect);

    /* Insert the message at the end */
    msgLength = strlen(formattedMsg);
    TESetSelect(32767, 32767, sDisplayTE);
    TEInsert(formattedMsg, msgLength, sDisplayTE);

    /* Update scrollbar and scroll to bottom if needed */
    UpdateTextScrollbar(sDisplayTE, sScrollBar, scrollToBottom);

    /* Ensure display never shows cursor */
    TEDeactivate(sDisplayTE);

    /* Redraw the display area */
    InvalRect(&sDisplayRect);

    /* Restore clipping */
    ClipRect(&saveClip);

    isAdding = false;
}

/* Function to toggle between AI models */
void ChatWindow_ToggleAIModel(void)
{
    char modelMsg[100];

    if (!sInitialized || !sIsVisible) {
        return;
    }

    /* Toggle the model */
    if (sActiveModel == kMarkovModel) {
        sActiveModel = kOpenAIModel;
    }
    else if (sActiveModel == kOpenAIModel) {
        sActiveModel = kTemplateModel;
    }
    else {
        sActiveModel = kMarkovModel;
    }

    /* Update model selection */
    SetActiveAIModel(sActiveModel);

    /* Inform the user about the change */
    if (sActiveModel == kMarkovModel) {
        strcpy(modelMsg, "Switched to Markov chain model.");
    }
    else if (sActiveModel == kOpenAIModel) {
        strcpy(modelMsg, "Switched to OpenAI model.");
    }
    else if (sActiveModel == kTemplateModel) {
        strcpy(modelMsg, "Switched to Template-based model.");
    }

    /* Add message to chat window */
    FormatAndAddMessage(modelMsg, false);

    /* Ensure the message is visible */
    if (sWindow != NULL) {
        InvalRect(&sDisplayRect);
    }
}

/* Refresh the conversation history display */
static void RefreshConversationDisplay(void)
{
    short i, idx;
    short maxMessages           = gConversationHistory.count;
    static Boolean isRefreshing = false;

    /* Prevent recursive refreshes */
    if (isRefreshing || !sInitialized || sDisplayTE == NULL || *sDisplayTE == NULL) {
        isRefreshing = false;
        return;
    }

    isRefreshing = true;

    /* Use clipping to reduce flicker */
    GrafPtr savePort;
    Rect saveClip;
    GetPort(&savePort);
    SetPort(sWindow);
    saveClip = sWindow->clipRgn[0]->rgnBBox;
    ClipRect(&sDisplayRect);

    /* Clear the text display */
    TESetText("", 0, sDisplayTE);

    /* Build up text content */
    for (i = 0; i < maxMessages; i++) {
        /* Calculate index using circular buffer logic */
        idx = (gConversationHistory.head + i) % kMaxConversationHistory;

        ConversationMessage *msg = &gConversationHistory.messages[idx];
        if (msg->text[0] != '\0') {
            /* Add message to display */
            FormatAndAddMessage(msg->text, (msg->type == kUserMessage));
        }
    }

    /* Ensure display is deactivated */
    TEDeactivate(sDisplayTE);

    /* Restore clipping and trigger redraw */
    ClipRect(&saveClip);
    InvalRect(&sDisplayRect);

    isRefreshing = false;
}

/* Send a message from the chat input field */
void ChatWindow_SendMessage(void)
{
    char promptBuffer[kMaxPromptLength];
    char *response;
    short thinkingMsgStart = 0;
    short thinkingMsgEnd   = 0;

    if (!sInitialized || sWindow == NULL || sInputTE == NULL || sDisplayTE == NULL) {
        return;
    }

    /* Get the user's prompt */
    int promptLen = (*sInputTE)->teLength;
    if (promptLen <= 0) {
        return; /* Empty prompt */
    }

    /* Copy text safely */
    if (promptLen >= kMaxPromptLength) {
        promptLen = kMaxPromptLength - 1;
    }

    memset(promptBuffer, 0, kMaxPromptLength);
    if ((*sInputTE)->hText != NULL && *(*sInputTE)->hText != NULL) {
        BlockMoveData(*(*sInputTE)->hText, promptBuffer, promptLen);
        promptBuffer[promptLen] = '\0';

        /* Trim leading and trailing whitespace */
        TrimWhitespace(promptBuffer);

        /* Check if the message is now empty after trimming */
        if (promptBuffer[0] == '\0') {
            return; /* Empty prompt after trimming */
        }
    }
    else {
        return;
    }

    /* Clear input field */
    ClearChatInput();

    /* Add user prompt to conversation history */
    AddUserPrompt(promptBuffer);

    /* Add the user message to display */
    FormatAndAddMessage(promptBuffer, true);

    /* Add "Thinking..." indicator */
    FormatAndAddMessage("Processing your query...", false);

    /* Remember where thinking message ends */
    if (sDisplayTE != NULL && *sDisplayTE != NULL) {
        thinkingMsgEnd = (*sDisplayTE)->teLength;
    }

    /* Generate AI response */
    response = GenerateAIResponse(&gConversationHistory);

    /* Remove thinking message and add real response */
    if (sDisplayTE != NULL && *sDisplayTE != NULL) {
        /* Safely remove thinking message */
        short origLength = (*sDisplayTE)->teLength;
        char *text       = (char *)*(*sDisplayTE)->hText;
        char *thinksPos  = strstr(text, "AI: Processing your query...");

        if (thinksPos != NULL) {
            long offset = thinksPos - text;

            /* Delete the thinking message */
            TESetSelect(offset, offset + strlen("AI: Processing your query..."), sDisplayTE);
            TEDelete(sDisplayTE);
        }
    }

    /* Add the real response */
    AddAIResponse(response);
    FormatAndAddMessage(response, false);

    /* Update display */
    InvalRect(&sDisplayRect);
}

/* Add a message to the chat display */
void ChatWindow_AddMessage(const char *message, Boolean isUserMessage)
{
    if (!sInitialized || sWindow == NULL || message == NULL || strlen(message) == 0) {
        return;
    }

    FormatAndAddMessage(message, isUserMessage);
}

/* Handle all events for the chat window */
void ChatWindow_HandleEvent(EventRecord *event)
{
    if (!sIsVisible || sWindow == NULL) {
        return;
    }

    switch (event->what) {
    case keyDown: {
        char key            = event->message & charCodeMask;
        Boolean isShiftDown = (event->modifiers & shiftKey) != 0;
        Boolean isCmdDown   = (event->modifiers & cmdKey) != 0;
        ChatWindow_HandleKeyDown(key, isShiftDown, isCmdDown);
        break;
    }

    case mouseDown: {
        Point mousePt = event->where;
        GlobalToLocal(&mousePt);
        ChatWindow_HandleContentClick(mousePt);
        break;
    }

    case activateEvt: {
        Boolean becomingActive = (event->modifiers & activeFlag) != 0;
        ChatWindow_HandleActivate(becomingActive);
        break;
    }

    case updateEvt:
        if ((WindowPtr)event->message == sWindow) {
            ChatWindow_Update();
        }
        break;
    }
}

/* Show or hide the chat window */
void ChatWindow_Show(Boolean visible)
{
    if (!sInitialized || sWindow == NULL) {
        return;
    }

    if (visible) {
        /* Refresh conversation display */
        RefreshConversationDisplay();

        /* Show window */
        ShowWindow(sWindow);
        SelectWindow(sWindow);

        /* Update display */
        ChatWindow_Render();

        /* Make sure scrollbar is properly updated when showing the window */
        if (sScrollBar != NULL && sDisplayTE != NULL) {
            UpdateTextScrollbar(sDisplayTE, sScrollBar, true);
        }

        sIsVisible = true;
    }
    else {
        HideWindow(sWindow);
        sIsVisible = false;

        /* Reset chat history */
        SetActiveAIModel(sActiveModel);
        InitModels();
    }
}

/* Get the window reference for the chat window */
WindowRef ChatWindow_GetWindowRef(void)
{
    return sWindow;
}

/* Private helper functions for menu integration */
static Boolean IsDisplayActive(void)
{
    if (sDisplayTE == NULL || *sDisplayTE == NULL)
        return false;

    /* Check if display has a selection */
    return ((*sDisplayTE)->selStart < (*sDisplayTE)->selEnd);
}

static Boolean IsInputActive(void)
{
    if (sInputTE == NULL || *sInputTE == NULL)
        return false;

    /* Check if input field is active or has selection */
    return ((*sInputTE)->selStart < (*sInputTE)->selEnd) || ((*sInputTE)->active != 0);
}

/* Get the active TEHandle for text operations */
TEHandle ChatWindow_GetActiveTE(void)
{
    /* Prioritize display if it has a selection (for copy operations) */
    if (IsDisplayActive())
        return sDisplayTE;

    /* Otherwise return input field if available */
    if (sInputTE != NULL)
        return sInputTE;

    return NULL;
}

/* Determine if the display area has a selection */
Boolean ChatWindow_HasDisplaySelection(void)
{
    return IsDisplayActive();
}

/* Determine if the input area has a selection */
Boolean ChatWindow_HasInputSelection(void)
{
    return IsInputActive();
}

/* Perform a Cut operation on the appropriate TE */
void ChatWindow_CutText(void)
{
    /* Cut can only apply to input field */
    if (sInputTE != NULL && IsInputActive()) {
        TECut(sInputTE);
    }
}

/* Perform a Copy operation on the appropriate TE */
void ChatWindow_CopyText(void)
{
    TEHandle activeTE = ChatWindow_GetActiveTE();
    if (activeTE != NULL) {
        TECopy(activeTE);
    }
}

/* Perform a Paste operation on the input TE */
void ChatWindow_PasteText(void)
{
    /* Paste can only apply to input field */
    if (sInputTE != NULL) {
        TEPaste(sInputTE);
    }
}

/* Perform a Clear/Delete operation on the appropriate TE */
void ChatWindow_ClearText(void)
{
    /* Clear can only apply to input field */
    if (sInputTE != NULL && IsInputActive()) {
        TEDelete(sInputTE);
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

/* Perform idle processing */
void ChatWindow_Idle(void)
{
    if (!sInitialized || sWindow == NULL || !sIsVisible) {
        return;
    }

    /* Only do idle processing for input field - this is where cursor blinks */
    if (sInputTE != NULL) {
        TEIdle(sInputTE);
    }
}