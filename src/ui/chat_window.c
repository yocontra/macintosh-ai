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
static AIModelType sActiveModel = kTemplateModel; /* Default to Template-based model */

/* Private function declarations */
static void _UpdateScrollbarFromText(void);
static void _UpdateTextFromScrollbar(void);
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
    /* Set the active AI model and initialize (which adds welcome message) */
    SetActiveAIModel(sActiveModel);
    InitModels();

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
            sContentRect.right - kResponseMargin, sInputRect.top - kResponseMargin - 15);

    /* Setup display area - adjust right edge to leave room for scrollbar and bottom to avoid
     * overlapping prompt text. Add reasonable space (15 pixels) to prevent overlap */
    SetRect(&sDisplayRect, kResponseMargin, kResponseMargin,
            sContentRect.right - kResponseMargin - 16, sInputRect.top - kResponseMargin - 15);

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
    /* DISABLE auto-scrolling for display area since we're managing scrolling manually */
    TEFeatureFlag(teFAutoScroll, teBitClear, sDisplayTE);

    /* Make sure display area doesn't show cursor initially */
    TEDeactivate(sDisplayTE);

    /* Create the scroll bar control with proper Mac scrollbar style */
    sScrollBar = NewControl(sWindow, &scrollBarRect, "\p", /* No title for scroll bar */
                            true,                          /* Visible */
                            0,                             /* Initial value (start at top) */
                            0,                             /* Min value (always 0) */
                            0, /* Max value - will be set to text height - view height */
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

    /* Set proper text attributes */
    TextFont(kFontGeneva);
    TextSize(12);
    TextFace(normal);
    ForeColor(blackColor);

    /* Make the text field editable with cursor properly positioned */
    TESetSelect(0, 0, sInputTE);

    /* Enable auto-scrolling for the INPUT field only
       Note: Using TEFeatureFlag is the preferred approach over TEAutoView */
    TEFeatureFlag(teFAutoScroll, teBitSet, sInputTE);

    /* Initialize the chat display - empty at first - call static function */
    _UpdateScrollbarFromText(); /* Only update scrollbar, not text position */

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
    /* Safety check */
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

    /* Update the model selection in the AI interface */
    SetActiveAIModel(sActiveModel);

    /* Inform the user about the model change */
    char modelMsg[100];

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

/* Scrollbar action procedure - handle continuous scrolling */
static pascal void ScrollAction(ControlHandle control, short part)
{
    short value, min, max;
    short oldValue;
    short lineHeight = 12; /* Default line height */

    if (control == NULL || sDisplayTE == NULL || *sDisplayTE == NULL)
        return;

    /* Get current control values */
    oldValue = GetControlValue(control);
    value    = oldValue;
    min      = GetControlMinimum(control);
    max      = GetControlMaximum(control);

    /* If max is 0, there's nothing to scroll */
    if (max == 0)
        return;

    /* Try to get actual line height if possible */
    if ((*sDisplayTE)->teLength > 0) {
        lineHeight = TEGetHeight(0, 1, sDisplayTE);
        if (lineHeight < 10)
            lineHeight = 10;
    }

    /* View height is used for page scrolling */
    short viewHeight = (*sDisplayTE)->viewRect.bottom - (*sDisplayTE)->viewRect.top;

    /* Process based on which part was clicked */
    switch (part) {
    case inUpButton:
        /* Scroll up one line */
        value -= lineHeight;
        break;

    case inDownButton:
        /* Scroll down one line */
        value += lineHeight;
        break;

    case inPageUp:
        /* Scroll up one page (view height) */
        value -= viewHeight;
        break;

    case inPageDown:
        /* Scroll down one page (view height) */
        value += viewHeight;
        break;

    case inThumb:
        /* Thumb position is already set by Control Manager */
        value = GetControlValue(control);
        break;

    default:
        return; /* Unknown part */
    }

    /* Keep value within bounds */
    if (value < min)
        value = min;
    if (value > max)
        value = max;

    /* Only update if value changed */
    if (value != oldValue) {
        SetControlValue(control, value);
        _UpdateTextFromScrollbar();
        InvalRect(&sDisplayRect);
    }
}

/* Update the scrollbar to reflect TextEdit content, without updating the text position */
static void _UpdateScrollbarFromText(void)
{
    /* Enhanced safety checks for TextEdit and ScrollBar */
    if (sScrollBar == NULL || sDisplayTE == NULL || *sDisplayTE == NULL)
        return;

    /* Standard Mac approach for TextEdit scrolling */
    short viewHeight = (*sDisplayTE)->viewRect.bottom - (*sDisplayTE)->viewRect.top;

    /* Get total text height through TextEdit API */
    short textHeight = TEGetHeight(0, (*sDisplayTE)->teLength, sDisplayTE);

    /* Standard Mac calculation: max scrolling is the content height minus view height */
    short maxScroll = 0;
    if (textHeight > viewHeight) {
        /* Add a small buffer (3 pixels) to ensure we can scroll fully to the bottom */
        maxScroll = textHeight - viewHeight + 3;
    }

    /* Save current state for comparison */
    short oldMaxScroll  = GetControlMaximum(sScrollBar);
    short oldScrollPos  = GetControlValue(sScrollBar);
    Boolean wasAtBottom = (oldScrollPos >= oldMaxScroll - 15); /* Consistent 15-pixel threshold */
    Boolean wasDisabled = (oldMaxScroll <= 0);                 /* Disabled scrollbars have max=0 */

    /* Get current scroll position */
    short currentScrollPos = GetControlValue(sScrollBar);
    if (currentScrollPos > maxScroll) {
        currentScrollPos = maxScroll;
    }

    if (maxScroll <= 0) {
        /* Content fits in view, disable scrollbar */
        HiliteControl(sScrollBar, 255); /* 255 = disabled */
        SetControlMaximum(sScrollBar, 0);
        SetControlValue(sScrollBar, 0);
    }
    else {
        short scrollPos;

        /* Content is larger than view, ensure scrollbar is enabled */
        if (wasDisabled) {
            /* Only call HiliteControl if actually changing state to avoid flicker */
            HiliteControl(sScrollBar, 0); /* 0 = enabled */

            /* When enabling, make sure scrollbar is visible */
            if (sWindow != NULL) {
                Rect scrollRect;
                /* Get scrollbar rectangle in local coordinates */
                scrollRect = (*sScrollBar)->contrlRect;
                /* Force redraw of the scrollbar area */
                InvalRect(&scrollRect);
            }
        }

        /* Set maximum value */
        SetControlMaximum(sScrollBar, maxScroll);

        /* Determine scroll position */
        if (wasAtBottom || oldMaxScroll == 0) {
            /* Auto-scroll to bottom for new messages */
            scrollPos = maxScroll;
        }
        else {
            /* Keep same position if possible */
            scrollPos = oldScrollPos;
            if (scrollPos > maxScroll)
                scrollPos = maxScroll;
        }

        /* Update scrollbar value */
        SetControlValue(sScrollBar, scrollPos);

        /* Make sure scrollbar is visible by forcing a redraw if max changed significantly */
        if (abs(oldMaxScroll - maxScroll) > 10 && sWindow != NULL) {
            DrawControls(sWindow);
        }
    }
}

/* Update the TextEdit content to reflect scrollbar position */
static void _UpdateTextFromScrollbar(void)
{
    /* Enhanced safety checks for TextEdit and ScrollBar */
    if (sScrollBar == NULL || sDisplayTE == NULL || *sDisplayTE == NULL)
        return;

    /* Get current scrollbar value */
    short scrollPos = GetControlValue(sScrollBar);

    /* Calculate current position to reduce visual jumping */
    short currentPos = (*sDisplayTE)->viewRect.top - (*sDisplayTE)->destRect.top;
    short delta      = currentPos - scrollPos;

    /* Only scroll if position actually changed */
    if (delta != 0) {
        /* Single TEScroll call with the delta is more efficient and reduces flicker */
        TEScroll(0, delta, sDisplayTE);
    }
}

/* Update both scrollbar and text to maintain consistency */
static void _UpdateChatScrollbar(void)
{
    static Boolean isUpdating = false;

    /* Prevent recursive calls that could cause infinite scrolling */
    if (isUpdating) {
        return;
    }

    isUpdating = true;

    /* First update scrollbar values based on text content */
    _UpdateScrollbarFromText();

    /* Then update text position to match scrollbar */
    _UpdateTextFromScrollbar();

    /* Only invalidate the display area if window exists */
    if (sWindow != NULL) {
        /* Use ClipRect to limit redraw to just the display area for better performance */
        GrafPtr savePort;
        Rect saveClip;

        GetPort(&savePort);
        saveClip = savePort->clipRgn[0]->rgnBBox;

        /* Limit redraw to just the display area */
        ClipRect(&sDisplayRect);
        InvalRect(&sDisplayRect);

        /* Restore original clipping */
        ClipRect(&saveClip);
    }

    isUpdating = false;
}

/* Draw the chat input field */
static void DrawChatInput(void)
{
    Rect frameRect  = sInputRect;
    Rect shadowRect = frameRect;
    Rect innerRect  = frameRect;
    GrafPtr savePort;
    Rect origClip;

    /* Save original clipping */
    GetPort(&savePort);
    origClip = savePort->clipRgn[0]->rgnBBox;

    /* STEP 1: Draw the label text first, above the input area */
    /* Create a clean label clip region with no interference */
    Rect labelClip;
    SetRect(&labelClip, frameRect.left, frameRect.top - 15, /* Allow enough space above the input */
            frameRect.right, frameRect.top - 2);            /* Stop just before the input box */

    ClipRect(&labelClip);

    /* Clear the label area first */
    BackColor(whiteColor);
    EraseRect(&labelClip);

    /* Draw the label with proper text settings */
    MoveTo(frameRect.left + 5, frameRect.top - 5); /* Position text within the label area */
    TextFont(kFontGeneva);
    TextSize(9);
    TextFace(bold);
    ForeColor(blackColor);
    BackColor(whiteColor);
    TextMode(srcOr);

    DrawString("\pType your prompt and press Return (Shift+Return for new line):");
    TextFace(normal);

    /* STEP 2: Now draw the input box below the label */
    /* Set clip just for the input box and its shadow */
    Rect inputAreaClip = frameRect;
    inputAreaClip.left -= 5;
    inputAreaClip.right += 5;
    inputAreaClip.bottom += 5;
    inputAreaClip.top -= 1; /* Only allow 1 pixel above for the top border */
    ClipRect(&inputAreaClip);

    /* Draw shadow effect (standard on Mac) */
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

    /* Adjust text positioning with proper inset for cursor */
    if (sInputTE != NULL) {
        /* Proper inset for text in Mac UI guidelines */
        innerRect = frameRect;
        InsetRect(&innerRect, 3, 3);

        /* Only update the rects if they've changed */
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
            /* Make sure scrollbar is active before handling clicks */
            if (GetControlMaximum(sScrollBar) > 0) {
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
                            /* Update text position to match the new scrollbar value */
                            _UpdateTextFromScrollbar();

                            /* Force redraw */
                            InvalRect(&sDisplayRect);
                        }
                    }
                }
                else {
                    /* For other parts, use our action proc */
                    part = TrackControl(clickedControl, localPt, (ControlActionUPP)ScrollAction);
                }
            }
            return true; /* Click was in scrollbar area, so we handled it */
        }
    }

    /* Handle clicks in the chat display area */
    if (sDisplayTE != NULL && PtInRect(localPt, &sDisplayRect)) {
        GrafPtr savePort;
        Rect saveClip;

        /* Save current clipping to limit visual effects */
        GetPort(&savePort);
        saveClip = savePort->clipRgn[0]->rgnBBox;

        /* Limit updates to just the display area */
        ClipRect(&sDisplayRect);

        /* Get initial selection to check if it changes */
        short oldSelStart = (*sDisplayTE)->selStart;
        short oldSelEnd   = (*sDisplayTE)->selEnd;

        /* Temporarily activate for selection only, but don't show blinking cursor */
        TEActivate(sDisplayTE);

        /* Handle clicks in the text display - text selection */
        TEClick(localPt, false, sDisplayTE);

        /* Check if selection changed */
        Boolean selectionChanged =
            (oldSelStart != (*sDisplayTE)->selStart || oldSelEnd != (*sDisplayTE)->selEnd);

        /* Immediately deactivate the display to prevent cursor from showing */
        TEDeactivate(sDisplayTE);

        /* Make sure input field has focus for typing */
        if (sInputTE != NULL) {
            TEActivate(sInputTE);
        }

        /* Restore original clipping */
        ClipRect(&saveClip);

        /* Only invalidate the display area if selection changed */
        if (selectionChanged) {
            InvalRect(&sDisplayRect);
        }

        return true;
    }

    /* Check if the click is in the input field */
    if (sInputTE != NULL && PtInRect(localPt, &sInputRect)) {
        GrafPtr savePort;
        Rect saveClip;

        /* Save current clipping to limit visual effects */
        GetPort(&savePort);
        saveClip = savePort->clipRgn[0]->rgnBBox;

        /* Limit updates to just the input area */
        ClipRect(&sInputRect);

        /* Get initial selection and cursor position to check if it changes */
        short oldSelStart = (*sInputTE)->selStart;
        short oldSelEnd   = (*sInputTE)->selEnd;

        /* Ensure the input field is active */
        TEActivate(sInputTE);

        /* Handle the click for text cursor positioning */
        TEClick(localPt, false, sInputTE);

        /* Only invalidate if selection changed */
        if (oldSelStart != (*sInputTE)->selStart || oldSelEnd != (*sInputTE)->selEnd) {
            TEUpdate(&sInputRect, sInputTE);
        }

        /* Restore original clipping */
        ClipRect(&saveClip);

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
        /* Window being activated - only activate the input field */
        if (sInputTE != NULL) {
            TEActivate(sInputTE);
        }
        /* Chat display should NEVER be activated - we don't want cursor there */
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

    /* Force window redraw on activation state change */
    InvalRect(&sWindow->portRect);
}

/* Render the chat window contents */
void ChatWindow_Render(void)
{
    if (!sInitialized || sWindow == NULL) {
        return;
    }

    SetPort(sWindow);

    /* Erase the window area with white background */
    BackColor(whiteColor);
    EraseRect(&sWindow->portRect);

    /* Draw a frame around the display area first (behind scrollbar) */
    if (sDisplayTE != NULL) {
        PenNormal();
        PenSize(1, 1);
        FrameRect(&sDisplayRect);
    }

    /* Draw the scrollbar if available */
    if (sScrollBar != NULL) {
        /* First check if the scrollbar should be enabled based on current content */
        short viewHeight = 0, textHeight = 0;

        if (sDisplayTE != NULL && *sDisplayTE != NULL) {
            viewHeight = (*sDisplayTE)->viewRect.bottom - (*sDisplayTE)->viewRect.top;
            textHeight = TEGetHeight(0, (*sDisplayTE)->teLength, sDisplayTE);

            /* Enable or disable scrollbar based on content */
            if (textHeight > viewHeight) {
                HiliteControl(sScrollBar, 0); /* 0 = enabled */
            }
            else {
                HiliteControl(sScrollBar, 255); /* 255 = disabled */
            }
        }

        /* Now draw the controls */
        DrawControls(sWindow);
    }

    /* Draw the chat display area content */
    if (sDisplayTE != NULL) {
        GrafPtr savePort;
        Rect clipSave;

        /* Save current drawing environment */
        GetPort(&savePort);
        clipSave = savePort->clipRgn[0]->rgnBBox;

        /* Create proper inset for text only - don't erase the entire area which would erase the
         * border */
        Rect textClip = sDisplayRect;
        InsetRect(&textClip, 3, 3);

        /* First set proper colors */
        BackColor(whiteColor);
        ForeColor(blackColor);

        /* Draw the text content with proper clipping */
        ClipRect(&textClip);
        TEUpdate(&(*sDisplayTE)->viewRect, sDisplayTE);

        /* Restore original clipping */
        ClipRect(&clipSave);
    }

    /* Draw the input area with its label - DrawChatInput handles the proper drawing order */
    DrawChatInput();

    /* Update input text field if available */
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

    /* Get the update region to be more selective about redraws */
    RgnHandle updateRgn = NewRgn();
    if (updateRgn != NULL) {
        GetClip(updateRgn);

        /* Check if display area needs updating */
        Rect displayIntersect = sDisplayRect;
        if (RectInRgn(&displayIntersect, updateRgn)) {
            /* Only redraw display area parts */
            PenNormal();
            PenSize(1, 1);
            FrameRect(&sDisplayRect);

            /* Draw the scrollbar if in update region */
            if (sScrollBar != NULL) {
                Rect scrollRect = (*sScrollBar)->contrlRect;
                if (RectInRgn(&scrollRect, updateRgn)) {
                    DrawControls(sWindow);
                }
            }

            /* Draw the chat display content */
            if (sDisplayTE != NULL) {
                GrafPtr savePort;
                Rect clipSave;

                /* Save current drawing environment */
                GetPort(&savePort);
                clipSave = savePort->clipRgn[0]->rgnBBox;

                /* Create a properly inset clip region to prevent text from touching the border */
                Rect textClip = sDisplayRect;
                InsetRect(&textClip, 3, 3);

                /* Set proper colors */
                BackColor(whiteColor);
                ForeColor(blackColor);

                ClipRect(&textClip);

                /* Update the text content */
                TEUpdate(&(*sDisplayTE)->viewRect, sDisplayTE);

                /* Restore original clipping */
                ClipRect(&clipSave);
            }
        }

        /* Check if input area or its label needs updating */
        Rect inputIntersect   = sInputRect;
        Rect labelIntersect   = sInputRect;
        labelIntersect.bottom = sInputRect.top;
        labelIntersect.top    = sInputRect.top - 15; /* Match the label area in DrawChatInput */

        /* Expand input rect slightly to include borders */
        inputIntersect.left -= 2;
        inputIntersect.right += 2;
        inputIntersect.top -= 2;
        inputIntersect.bottom += 2;

        if (RectInRgn(&inputIntersect, updateRgn) || RectInRgn(&labelIntersect, updateRgn)) {
            /* DrawChatInput handles drawing both the label and input box in the correct order */
            DrawChatInput();

            if (sInputTE != NULL) {
                TEUpdate(&sInputRect, sInputTE);
            }
        }

        DisposeRgn(updateRgn);
    }
    else {
        /* Fallback if we couldn't allocate the region */
        ChatWindow_Render();
    }

    EndUpdate(sWindow);
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
    } break;

    case mouseDown: {
        Point mousePt = event->where;
        GlobalToLocal(&mousePt);
        ChatWindow_HandleContentClick(mousePt);
    } break;

    case activateEvt: {
        Boolean becomingActive = (event->modifiers & activeFlag) != 0;
        ChatWindow_HandleActivate(becomingActive);
    } break;

    case updateEvt:
        if ((WindowPtr)event->message == sWindow) {
            ChatWindow_Update();
        }
        break;

    default:
        /* No other event types need to be handled */
        break;
    }
}

/* Clear the chat input field */
static void ClearChatInput(void)
{
    /* Additional safety checks */
    if (!sInitialized || sWindow == NULL || sInputTE == NULL) {
        return;
    }

    GrafPtr savePort;
    Rect saveClip;

    /* Save current clipping to limit visual effects */
    GetPort(&savePort);
    saveClip = savePort->clipRgn[0]->rgnBBox;

    /* Prepare the field area with proper white background before clearing */
    Rect frameRect = sInputRect;
    InsetRect(&frameRect, 1, 1); /* Same inset as in DrawChatInput */

    /* Limit drawing to just the input field area */
    ClipRect(&frameRect);

    /* Fill with white before clearing to prevent black flash */
    BackColor(whiteColor);
    EraseRect(&frameRect);

    /* First completely clear the field's content */
    TESetSelect(0, 32767, sInputTE); /* Select all text */
    TEDelete(sInputTE);              /* Delete all selected text */
    TESetSelect(0, 0, sInputTE);     /* Place cursor at beginning */

    /* Make sure the input field remains active */
    TEActivate(sInputTE);

    /* Update the TextEdit field immediately (more controlled than InvalRect) */
    TEUpdate(&frameRect, sInputTE);

    /* Restore original clipping */
    ClipRect(&saveClip);
}

/* Format a new message with proper styling */
static void FormatAndAddMessage(const char *message, Boolean isUserMessage)
{
    char formattedMsg[kMaxPromptLength + 100]; /* Extra space for formatting */
    short msgLength;
    static Boolean isAdding = false; /* Prevent recursive calls */

    /* Track scroll state */
    short oldScrollPos  = 0;
    short maxScroll     = 0;
    Boolean wasAtBottom = true; /* Default to auto-scrolling */

    /* Prevent recursion through RefreshConversationDisplay */
    if (isAdding) {
        return;
    }

    isAdding = true;

    if (message == NULL || sDisplayTE == NULL || *sDisplayTE == NULL) {
        isAdding = false;
        return;
    }

    /* Safety check for message length */
    if (strlen(message) == 0 || strlen(message) >= kMaxPromptLength) {
        isAdding = false;
        return;
    }

    /* Check if user was at the bottom before adding message (for auto-scroll) */
    if (sScrollBar != NULL) {
        maxScroll    = GetControlMaximum(sScrollBar);
        oldScrollPos = GetControlValue(sScrollBar);

        /* More generous threshold of 15 pixels to detect "near bottom" */
        wasAtBottom = (oldScrollPos >= maxScroll - 15);
    }

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
    }
    else {
        /* Simplified header */
        strncat(formattedMsg, "AI: ", remainingSpace);
        remainingSpace -= strlen("AI: ");
    }

    /* Add truncated message safely */
    strncat(formattedMsg, message,
            (safeMessageLen < remainingSpace) ? safeMessageLen : remainingSpace);

    /* Save current selection for possible restoration */
    short selStart       = (*sDisplayTE)->selStart;
    short selEnd         = (*sDisplayTE)->selEnd;
    Boolean hasSelection = (selStart < selEnd);

    /* Set text style before inserting */
    TextFont(kFontMonaco); /* Monaco is monospaced and works better for ASCII art */
    TextSize(10);

    if (isUserMessage) {
        TextFace(bold);
    }
    else {
        TextFace(normal);
    }

    /* Use clipping to reduce flicker during text insertion */
    GrafPtr savePort;
    Rect saveClip;
    GetPort(&savePort);
    saveClip = savePort->clipRgn[0]->rgnBBox;

    /* Limit updates to just the display area while modifying text */
    ClipRect(&sDisplayRect);

    /* Insert the new message at the end */
    msgLength = strlen(formattedMsg);
    TESetSelect(32767, 32767, sDisplayTE); /* Move to end */
    TEInsert(formattedMsg, msgLength, sDisplayTE);

    /* Update the scrollbar values to reflect new content */
    _UpdateScrollbarFromText();

    /* Unified approach to scroll position: use the same logic as RefreshConversationDisplay */
    if (wasAtBottom) {
        /* Auto-scroll to bottom */
        if (sScrollBar != NULL) {
            SetControlValue(sScrollBar, GetControlMaximum(sScrollBar));
        }
    }
    else if (hasSelection && selEnd <= (*sDisplayTE)->teLength - msgLength) {
        /* Maintain selection */
        TESetSelect(selStart, selEnd, sDisplayTE);

        /* Restore original scroll position */
        if (sScrollBar != NULL) {
            SetControlValue(sScrollBar, oldScrollPos);
        }
    }
    else {
        /* Maintain scroll position for reading previous content */
        if (sScrollBar != NULL) {
            SetControlValue(sScrollBar, oldScrollPos);
        }
    }

    /* Update the text position to match scrollbar */
    _UpdateTextFromScrollbar();

    /* Restore original clipping */
    ClipRect(&saveClip);

    /* Make sure display area never shows cursor */
    TEDeactivate(sDisplayTE);

    /* Limit redraw to just the display area for better performance */
    if (sWindow != NULL) {
        InvalRect(&sDisplayRect); /* Only invalidate display area, not entire window */
    }

    isAdding = false;
}

/* Send a message from the chat input field */
void ChatWindow_SendMessage(void)
{
    char promptBuffer[kMaxPromptLength];
    char *response;
    short thinkingMsgStart = 0;
    short thinkingMsgEnd   = 0;

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

    /* Limit visual updates during thinking indicator */
    GrafPtr savePort;
    Rect saveClip;
    GetPort(&savePort);
    saveClip = savePort->clipRgn[0]->rgnBBox;

    /* Remember the position before adding thinking indicator */
    if (sDisplayTE != NULL && *sDisplayTE != NULL) {
        thinkingMsgStart = (*sDisplayTE)->teLength;
    }

    /* Add temporary "Thinking..." indicator directly at end of text */
    char thinkingMsg[80];
    sprintf(thinkingMsg, "Processing your query...");

    /* Add thinking message to display (but NOT to conversation history) */
    ClipRect(&sDisplayRect);
    FormatAndAddMessage(thinkingMsg, false); /* false = AI message */

    /* Remember where the thinking message ends */
    if (sDisplayTE != NULL && *sDisplayTE != NULL) {
        thinkingMsgEnd = (*sDisplayTE)->teLength;
    }

    /* Ensure the thinking message is visible with targeted redraw */
    InvalRect(&sDisplayRect);
    ClipRect(&saveClip);

    /* Generate AI response using the selected model */
    response = GenerateAIResponse(&gConversationHistory);

    /* Remove the temporary thinking message directly */
    if (sDisplayTE != NULL && *sDisplayTE != NULL && thinkingMsgStart < thinkingMsgEnd) {
        /* Use clipping to reduce flicker when removing thinking message */
        ClipRect(&sDisplayRect);

        /* Delete the thinking message by replacing with empty string */
        TESetSelect(thinkingMsgStart, thinkingMsgEnd, sDisplayTE);
        TEDelete(sDisplayTE);

        /* Reset any selections */
        TESetSelect(thinkingMsgEnd, thinkingMsgEnd, sDisplayTE);

        /* Add the real response directly - more efficient than a full refresh */
        AddAIResponse(response);
        FormatAndAddMessage(response, false); /* false = AI message */

        /* Restore original clipping */
        ClipRect(&saveClip);
    }
    else {
        /* Fallback if direct removal fails */
        AddAIResponse(response);
        RefreshConversationDisplay();
    }

    /* Targeted invalidation of just the display area */
    if (sWindow != NULL) {
        InvalRect(&sDisplayRect);
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
    short i, idx;
    short maxMessages           = gConversationHistory.count;
    static Boolean isRefreshing = false; /* Prevent recursive refreshes */

    /* Prevent recursive refreshes that could cause infinite scrolling loops */
    if (isRefreshing) {
        return;
    }

    isRefreshing = true;

    /* Safety check */
    if (!sInitialized || sDisplayTE == NULL || *sDisplayTE == NULL) {
        isRefreshing = false;
        return;
    }

    /* Store current scroll position information before clearing */
    short oldMaxScroll   = 0;
    short oldScrollPos   = 0;
    Boolean wasAtBottom  = true; /* Default to scrolling to bottom */
    Boolean hasSelection = false;
    short selStart = 0, selEnd = 0;

    /* Save current selection state */
    if (sDisplayTE != NULL && *sDisplayTE != NULL) {
        selStart     = (*sDisplayTE)->selStart;
        selEnd       = (*sDisplayTE)->selEnd;
        hasSelection = (selStart < selEnd);
    }

    if (sScrollBar != NULL) {
        oldMaxScroll = GetControlMaximum(sScrollBar);
        oldScrollPos = GetControlValue(sScrollBar);

        /* Consistent with FormatAndAddMessage, use 15 pixel threshold */
        wasAtBottom = (oldScrollPos >= oldMaxScroll - 15);
    }

    /* Use clipping to reduce flicker during text rebuild */
    GrafPtr savePort;
    Rect saveClip;
    GetPort(&savePort);
    SetPort(sWindow);
    saveClip = sWindow->clipRgn[0]->rgnBBox;

    /* Limit updates to just the display area */
    ClipRect(&sDisplayRect);

    /* Clear the text display first */
    TESetText("", 0, sDisplayTE);

    /* First build up all the text content without scrolling */
    for (i = 0; i < maxMessages; i++) {
        /* Calculate the index using the circular buffer logic - start at head and wrap around */
        idx = (gConversationHistory.head + i) % kMaxConversationHistory;

        ConversationMessage *msg = &gConversationHistory.messages[idx];
        if (msg->text[0] != '\0') {
            /* Format message for display */
            char formattedMsg[kMaxPromptLength + 100]; /* Extra space for formatting */
            short msgLength;

            /* Format the message with proper header and indentation */
            memset(formattedMsg, 0, sizeof(formattedMsg)); /* Clear entire buffer */

            /* Add header and message */
            if (msg->type == kUserMessage) {
                strcat(formattedMsg, "\rYou: ");
                strcat(formattedMsg, msg->text);
            }
            else {
                strcat(formattedMsg, "\rAI: ");
                strcat(formattedMsg, msg->text);
            }

            /* Set text style */
            TextFont(kFontMonaco);
            TextSize(10);

            if (msg->type == kUserMessage) {
                TextFace(bold);
            }
            else {
                TextFace(normal);
            }

            /* Add to display */
            msgLength = strlen(formattedMsg);
            TESetSelect(32767, 32767, sDisplayTE);
            TEInsert(formattedMsg, msgLength, sDisplayTE);
        }
    }

    /* Update scrollbar based on current text content */
    _UpdateScrollbarFromText();

    /* Use consistent scroll position logic with FormatAndAddMessage */
    if (wasAtBottom) {
        /* Auto-scroll to bottom */
        if (sScrollBar != NULL) {
            SetControlValue(sScrollBar, GetControlMaximum(sScrollBar));
        }
    }
    else {
        /* When refreshing, we can't maintain exact position and selection,
           so we use proportional scrolling to maintain relative position */
        short newMaxScroll = GetControlMaximum(sScrollBar);

        if (oldMaxScroll > 0 && newMaxScroll > 0) {
            /* Calculate relative position and apply to new content */
            float scrollRatio  = (float)oldScrollPos / (float)oldMaxScroll;
            short newScrollPos = (short)(scrollRatio * newMaxScroll);

            /* Ensure we stay within valid range */
            if (newScrollPos > newMaxScroll)
                newScrollPos = newMaxScroll;
            if (newScrollPos < 0)
                newScrollPos = 0;

            /* Apply the new position */
            SetControlValue(sScrollBar, newScrollPos);
        }
    }

    /* Update text position to match scrollbar */
    _UpdateTextFromScrollbar();

    /* Ensure display is still deactivated */
    TEDeactivate(sDisplayTE);

    /* Restore original clipping and trigger a focused redraw */
    ClipRect(&saveClip);
    InvalRect(&sDisplayRect);

    isRefreshing = false;
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

        /* Show and select the window */
        ShowWindow(sWindow);
        SelectWindow(sWindow);

        /* Render the window contents */
        ChatWindow_Render();

        sIsVisible = true;
    }
    else {
        HideWindow(sWindow);
        sIsVisible = false;

        /* Reset chat history and reinitialize the model when closing window */
        SetActiveAIModel(sActiveModel); /* Ensure active model is set correctly */
        InitModels();
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

    /* Only do idle processing for input field - this is where cursor blinks */
    if (sInputTE != NULL) {
        TEIdle(sInputTE);
    }

    /* Explicitly NOT calling TEIdle for display area to avoid cursor */

    /* Ensure display area remains deactivated (no cursor) but don't adjust scrolling */
    if (sDisplayTE != NULL) {
        TEDeactivate(sDisplayTE);
    }
}