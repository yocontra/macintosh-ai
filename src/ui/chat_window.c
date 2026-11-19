#include <ControlDefinitions.h>
#include <Controls.h>
#include <Fonts.h>
#include <Memory.h>
#include <Quickdraw.h>
#include <Sound.h>
#include <TextEdit.h>
#include <TextUtils.h>
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../chatbot/api_client.h"
#include "../chatbot/model_manager.h"
#include "../constants.h"
#include "../error.h"
#include "chat_window.h"
#include "utils.h"

/* Chat layout, window-local pixels. This is the only place these numbers live. */
enum {
    kChatEdge        = 12, /* window edge to the chrome blocks */
    kChatHeaderH     = 20, /* header strip height */
    kChatGap         = 8,  /* between logical blocks */
    kChatTextPadX    = 8,  /* transcript text inset inside its well */
    kChatTextPadY    = 6,
    kChatScrollW     = 16, /* scrollbar width */
    kChatScrollInset = 4,  /* scrollbar inset inside the well */
    kChatInputH      = 44, /* input well height */
    kChatLabelColumn = 56, /* "Message:" column left of the input well */
    kChatRuleOffset  = 6,  /* input top to the rule above it */
    kChatFieldPadX   = 5,  /* text inset inside the input well */
    kChatFieldPadY   = 4,
    kChatBodySize    = 9,  /* Monaco: transcript and input */
    kChatLabelSize   = 9,  /* Geneva: header, status, "Message:" */
    kChatHeaderBase  = 6,  /* header bottom to the text baseline */
    kChatLabelBase   = 13  /* input top to the "Message:" baseline */
};

/* Module variables */
static WindowRef sWindow    = NULL;
static Boolean sInitialized = false;
static Boolean sIsVisible   = false;
static Rect sContentRect;               /* Main content area */
static Rect sInputRect;                 /* Input field at bottom */
static Rect sDisplayRect;               /* Display area for conversation */
static TEHandle sInputTE        = NULL; /* Text edit handle for input */
static TEHandle sDisplayTE      = NULL; /* Text edit handle for chat display */
static ControlHandle sScrollBar = NULL; /* Scrollbar for chat display */
static TEHandle sFocusTE        = NULL; /* Field that owns the insertion point */

/* Asynchronous request state */
static Boolean sAwaiting     = false; /* An API request is in flight */
static short sResponseStart  = 0;     /* Display offset just after the user turn */
static short sResponseLength = 0;     /* Bytes accumulated in sResponseBuffer */
static char sResponseBuffer[kMaxResponseLength + 1];

/* Set while the transcript is being rebuilt from history */
static Boolean sRefreshing = false;

/* Private function declarations */
static void ChatWindow_Update(void);
static void ChatWindow_Render(void);
static void ChatWindow_HandleActivate(Boolean becomingActive);
static Boolean ChatWindow_HandleContentClick(Point localPt);
static Boolean ChatWindow_HandleKeyDown(char key, Boolean isShiftDown, Boolean isCmdDown);
static void FormatAndAddMessage(const char *message, Boolean isUserMessage);
static void RefreshConversationDisplay(void);
static void AppendDisplayText(const char *text, short len);
static void CancelResponse(void);
static void DrawChatInput(void);
static void ClearChatInput(void);
static void Chat_HeaderRect(Rect *out);
static void Chat_StatusText(char *out, short max);
static void DrawInsetWell(const Rect *bounds);
static void DrawChatHeader(void);
static void DrawChatRule(void);
static pascal void ScrollAction(ControlHandle control, short part);

/* Initialize and create the chat window */
Boolean ChatWindow_Initialize(void)
{
    Rect windowRect, viewRect, destRect, scrollBarRect;
    short screenWidth, screenHeight, windowWidth, windowHeight;
    OSErr err;

    /* Prevent double initialization */
    if (sInitialized) {
        return true;
    }

    /* Initialize the AI models */
    InitModels();

    /* Check available memory */
    err = CheckMemory();
    if (err != noErr) {
        HandleError(kErrMemoryFull, kCtxOpeningChatWindow, false); /* Non-fatal */
        return false;
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
        return false;
    }

    SetPort(sWindow);

    /* Setup main content area (whole window) */
    sContentRect = sWindow->portRect;

    /* Input well at the bottom, with a short label column to its left */
    SetRect(&sInputRect, sContentRect.left + kChatEdge + kChatLabelColumn,
            sContentRect.bottom - kChatEdge - kChatInputH, sContentRect.right - kChatEdge,
            sContentRect.bottom - kChatEdge);

    /* Transcript well: below the header strip, above the rule over the input */
    {
        Rect headerRect;
        short ruleY, wellBottom;

        Chat_HeaderRect(&headerRect);
        ruleY      = sInputRect.top - kChatRuleOffset;
        wellBottom = ruleY - kChatGap;

        SetRect(&sDisplayRect, sContentRect.left + kChatEdge, headerRect.bottom + kChatGap,
                sContentRect.right - kChatEdge, wellBottom);
    }

    /* Scrollbar sits inside the well, not glued to its frame */
    SetRect(&scrollBarRect, sDisplayRect.right - kChatScrollInset - kChatScrollW,
            sDisplayRect.top + kChatScrollInset, sDisplayRect.right - kChatScrollInset,
            sDisplayRect.bottom - kChatScrollInset);

    /* Text lives inside the well with real padding */
    viewRect.left   = sDisplayRect.left + kChatTextPadX;
    viewRect.top    = sDisplayRect.top + kChatTextPadY;
    viewRect.right  = scrollBarRect.left - kChatTextPadX;
    viewRect.bottom = sDisplayRect.bottom - kChatTextPadY;
    destRect        = viewRect;

    /* TENew copies the port's text state into the record, so set Monaco 9
       before creating either field; that also gives the right line height. */
    TextFont(kFontMonaco);
    TextSize(kChatBodySize);
    TextFace(normal);

    /* Create text display with shared utility function */
    sDisplayTE = CreateStandardTextField(&viewRect, &destRect, false);
    if (sDisplayTE == NULL) {
        HandleError(kErrTextEditCreation, kCtxCreatingResponseArea, false);
        DisposeWindow(sWindow);
        sWindow = NULL;
        return false;
    }

    /* Create the scroll bar */
    sScrollBar =
        NewControl(sWindow, &scrollBarRect, "\p", true, 0, 0, 0, scrollBarProc, (long)sDisplayTE);

    if (sScrollBar == NULL) {
        HandleError(kErrControlCreation, kCtxCreatingResponseArea, false);
    }

    /* Create chat input text field, padded inside its well */
    viewRect.left   = sInputRect.left + kChatFieldPadX;
    viewRect.top    = sInputRect.top + kChatFieldPadY;
    viewRect.right  = sInputRect.right - kChatFieldPadX;
    viewRect.bottom = sInputRect.bottom - kChatFieldPadY;
    destRect        = viewRect;

    sInputTE = CreateStandardTextField(&viewRect, &destRect, true);
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
        return false;
    }

    /* Update scrollbar to reflect empty text and make sure it has the right state */
    UpdateTextScrollbar(sDisplayTE, sScrollBar, true);
    if (sScrollBar != NULL) {
        HiliteControl(sScrollBar, 255); /* Initially disabled until we have content */
    }

    sInitialized = true;
    sIsVisible   = false;
    sFocusTE     = sInputTE;

    return true;
}

/* Dispose of the chat window and clean up resources */
void ChatWindow_Dispose(void)
{
    if (!sInitialized) {
        return;
    }

    /* The quit path shuts the client down before disposing windows, so only
       forget the request here rather than calling into a dead client. */
    sAwaiting          = false;
    sResponseLength    = 0;
    sResponseBuffer[0] = '\0';

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
    lineHeight = (*sDisplayTE)->lineHeight;
    if (lineHeight < 10)
        lineHeight = 10;

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

/* Live status for the header. The provider slice owns the wording and knows
   the active model, the key state and the TLS setting. */
static void Chat_StatusText(char *out, short max)
{
    if (out == NULL || max <= 0) {
        return;
    }

    out[0] = '\0';
    API_GetStatusText(out, max);
}

/* The header strip is derived from the content rect every time. */
static void Chat_HeaderRect(Rect *out)
{
    SetRect(out, sContentRect.left + kChatEdge, sContentRect.top + kChatEdge,
            sContentRect.right - kChatEdge, sContentRect.top + kChatEdge + kChatHeaderH);
}

/* The status text in the header changes with the model and the request state. */
static void Chat_InvalidateHeader(void)
{
    Rect header;

    Chat_HeaderRect(&header);
    InvalRect(&header);
}

/* Platinum-style content well: white fill, one-pixel black outline, white just
   inside the top and left, dark grey just inside the bottom and right. Port,
   pen, colour and pattern are restored. */
static void DrawInsetWell(const Rect *bounds)
{
    Rect r;
    GrafPtr savePort;
    PenState savePen;
    Pattern saveBackPat;
    RGBColor saveFore, saveBack;

    if (bounds == NULL) {
        return;
    }

    GetPort(&savePort);
    GetPenState(&savePen);
    GetForeColor(&saveFore);
    GetBackColor(&saveBack);
    { GrafPtr p; GetPort(&p); saveBackPat = p->bkPat; }

    r = *bounds;
    {
        static const RGBColor white = {0xFFFF, 0xFFFF, 0xFFFF};

        RGBBackColor(&white);
    }
    BackPat(&qd.white);
    EraseRect(&r);

    PenSize(1, 1);
    PenPat(&qd.black);
    ForeColor(blackColor);
    FrameRect(&r);

    InsetRect(&r, 1, 1);

    PenPat(&qd.white);
    ForeColor(whiteColor);
    MoveTo(r.left, r.bottom - 1);
    LineTo(r.left, r.top);
    LineTo(r.right - 1, r.top);

    PenPat(&qd.dkGray);
    ForeColor(blackColor);
    MoveTo(r.right - 1, r.top);
    LineTo(r.right - 1, r.bottom - 1);
    LineTo(r.left, r.bottom - 1);

    SetPenState(&savePen);
    RGBForeColor(&saveFore);
    RGBBackColor(&saveBack);
    BackPat(&saveBackPat);
    SetPort(savePort);
}

/* Header strip: app name left, live status right, one grey rule underneath. */
static void DrawChatHeader(void)
{
    Rect header;
    GrafPtr savePort;
    PenState savePen;
    Pattern saveBackPat;
    RGBColor saveFore, saveBack;
    short saveFont, saveSize, saveFace;
    char status[96];
    Str255 statusStr;
    RGBColor smallPrint = {0x8888, 0x8888, 0x8888};

    GetPort(&savePort);
    SetPort(sWindow);
    GetPenState(&savePen);
    GetForeColor(&saveFore);
    GetBackColor(&saveBack);
    { GrafPtr p; GetPort(&p); saveBackPat = p->bkPat; }
    saveFont = sWindow->txFont;
    saveSize = sWindow->txSize;
    saveFace = sWindow->txFace;

    Chat_HeaderRect(&header);

    /* Solid grey strip: a 1-bit pattern here dithers into stripes on a colour
     * display and the app name and status became unreadable. */
    {
        static const RGBColor headerTint = {0xCCCC, 0xCCCC, 0xCCCC};

        RGBBackColor(&headerTint);
    }
    BackPat(&qd.white);
    EraseRect(&header);

    TextFont(kFontGeneva);
    TextSize(kChatLabelSize);
    TextFace(bold);
    ForeColor(blackColor);
    MoveTo(header.left, header.bottom - kChatHeaderBase);
    DrawString("\pMacintosh AI");

    Chat_StatusText(status, (short)sizeof(status));
    CStrToPStr(status, statusStr);

    TextFace(normal);
    RGBForeColor(&smallPrint);
    MoveTo(header.right - StringWidth(statusStr), header.bottom - kChatHeaderBase);
    DrawString(statusStr);

    PenSize(1, 1);
    PenPat(&qd.dkGray);
    ForeColor(blackColor);
    MoveTo(header.left, header.bottom);
    LineTo(header.right, header.bottom);

    SetPenState(&savePen);
    RGBForeColor(&saveFore);
    RGBBackColor(&saveBack);
    BackPat(&saveBackPat);
    TextFont(saveFont);
    TextSize(saveSize);
    TextFace(saveFace);
    SetPort(savePort);
}

/* One-pixel grey rule between the transcript and the input well. */
static void DrawChatRule(void)
{
    short y = sInputRect.top - kChatRuleOffset;
    GrafPtr savePort;
    PenState savePen;
    RGBColor saveFore;

    GetPort(&savePort);
    SetPort(sWindow);
    GetPenState(&savePen);
    GetForeColor(&saveFore);

    PenSize(1, 1);
    PenPat(&qd.dkGray);
    ForeColor(blackColor);
    MoveTo(sContentRect.left + kChatEdge, y);
    LineTo(sContentRect.right - kChatEdge, y);

    SetPenState(&savePen);
    RGBForeColor(&saveFore);
    SetPort(savePort);
}

/* Draw the "Message:" label and the input well. */
static void DrawChatInput(void)
{
    GrafPtr savePort;
    PenState savePen;
    RGBColor saveFore;
    short saveFont, saveSize, saveFace;

    GetPort(&savePort);
    SetPort(sWindow);
    GetPenState(&savePen);
    GetForeColor(&saveFore);
    saveFont = sWindow->txFont;
    saveSize = sWindow->txSize;
    saveFace = sWindow->txFace;

    DrawInsetWell(&sInputRect);

    TextFont(kFontGeneva);
    TextSize(kChatLabelSize);
    TextFace(bold);
    ForeColor(blackColor);
    MoveTo(sContentRect.left + kChatEdge, sInputRect.top + kChatLabelBase);
    DrawString("\pMessage:");

    SetPenState(&savePen);
    RGBForeColor(&saveFore);
    TextFont(saveFont);
    TextSize(saveSize);
    TextFace(saveFace);
    SetPort(savePort);
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

        if (sInputTE != NULL) {
            TEDeactivate(sInputTE);
        }
        TEActivate(sDisplayTE);
        TEClick(localPt, false, sDisplayTE);
        sFocusTE = sDisplayTE;

        Boolean selectionChanged =
            (oldSelStart != (*sDisplayTE)->selStart || oldSelEnd != (*sDisplayTE)->selEnd);

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

        if (sDisplayTE != NULL) {
            TEDeactivate(sDisplayTE);
        }
        TEActivate(sInputTE);
        TEClick(localPt, false, sInputTE);
        sFocusTE = sInputTE;

        if (oldSelStart != (*sInputTE)->selStart || oldSelEnd != (*sInputTE)->selEnd) {
            TEUpdate(&(*sInputTE)->viewRect, sInputTE);
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

        case '.' /* Cmd-Period: cancel a request or clear text */:
            if (sAwaiting) {
                CancelResponse();
            }
            else {
                ChatWindow_ClearText();
            }
            return true;
        }
    }

    /* Enforce the prompt limit while typing instead of silently truncating */
    if (key >= ' ' && (*sInputTE)->teLength >= kMaxPromptLength - 1) {
        SysBeep(1);
        return true;
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
        /* Restore the insertion point to whichever field had it */
        if (sFocusTE == sDisplayTE && sDisplayTE != NULL) {
            TEActivate(sDisplayTE);
        }
        else if (sInputTE != NULL) {
            sFocusTE = sInputTE;
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
    GrafPtr savePort;
    Rect teRect;

    if (!sInitialized || sWindow == NULL) {
        return;
    }

    GetPort(&savePort);
    SetPort(sWindow);

    /* Chrome is a flat grey; the content lives in white wells. A 1-bit pattern
     * dithers into visible noise on a 256-colour Mac (the header and the whole
     * transcript were unreadable), so tint with the background colour and fill
     * with an all-ones pattern instead. */
    {
        static const RGBColor chrome = {0xCCCC, 0xCCCC, 0xCCCC};

        RGBBackColor(&chrome);
        BackPat(&qd.white);
        EraseRect(&sWindow->portRect);
    }
    PenNormal();

    DrawChatHeader();
    DrawChatRule();
    DrawInsetWell(&sDisplayRect);

    /* Draw the scrollbar, enabled only when the text is taller than its view */
    if (sScrollBar != NULL) {
        short viewHeight = 0, textHeight = 0;

        if (sDisplayTE != NULL && *sDisplayTE != NULL) {
            viewHeight = (*sDisplayTE)->viewRect.bottom - (*sDisplayTE)->viewRect.top;
            textHeight = (short)((*sDisplayTE)->nLines * (*sDisplayTE)->lineHeight);

            HiliteControl(sScrollBar, textHeight > viewHeight ? 0 : 255);
        }

        DrawControls(sWindow);
    }

    /* Draw the transcript inside its well */
    if (sDisplayTE != NULL && *sDisplayTE != NULL) {
        teRect = (*sDisplayTE)->viewRect;
        BackColor(whiteColor);
        ForeColor(blackColor);
        TEUpdate(&teRect, sDisplayTE);
    }

    /* Draw the input well and its text */
    DrawChatInput();

    if (sInputTE != NULL && *sInputTE != NULL) {
        teRect = (*sInputTE)->viewRect;
        BackColor(whiteColor);
        ForeColor(blackColor);
        TEUpdate(&teRect, sInputTE);
    }

    SetPort(savePort);
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
    RGBColor saveBack;
    Pattern saveBackPat;

    GetPort(&savePort);
    saveClip = savePort->clipRgn[0]->rgnBBox;
    GetBackColor(&saveBack);
    { GrafPtr p; GetPort(&p); saveBackPat = p->bkPat; }

    Rect frameRect = sInputRect;
    InsetRect(&frameRect, 2, 2);
    ClipRect(&frameRect);

    BackPat(&qd.white);
    BackColor(whiteColor);
    EraseRect(&frameRect);

    TESetSelect(0, 32767, sInputTE);
    TEDelete(sInputTE);
    TESetSelect(0, 0, sInputTE);

    TEActivate(sInputTE);
    TEUpdate(&(*sInputTE)->viewRect, sInputTE);
    sFocusTE = sInputTE;

    ClipRect(&saveClip);
    RGBBackColor(&saveBack);
    BackPat(&saveBackPat);
}

/* Format a new message with proper styling */
static void FormatAndAddMessage(const char *message, Boolean isUserMessage)
{
    char formattedMsg[kMaxResponseLength + 8];
    const char *prefix;
    short prefixLen, msgLen, maxMsg, leadLen, start;
    static Boolean isAdding = false;

    /* Prevent recursive calls */
    if (isAdding || message == NULL || sDisplayTE == NULL || *sDisplayTE == NULL) {
        return;
    }

    msgLen = (short)strlen(message);
    if (msgLen == 0) {
        return;
    }

    prefix    = isUserMessage ? "You: " : "AI: ";
    prefixLen = (short)strlen(prefix);

    /* Leave room for the trailing CR and the terminating NUL */
    maxMsg = (short)(sizeof(formattedMsg) - prefixLen - 2);
    if (msgLen > maxMsg) {
        msgLen = maxMsg;
    }

    isAdding = true;

    memcpy(formattedMsg, prefix, (size_t)prefixLen);
    memcpy(formattedMsg + prefixLen, message, (size_t)msgLen);
    formattedMsg[prefixLen + msgLen]     = '\r';
    formattedMsg[prefixLen + msgLen + 1] = '\0';

    /* A transcript cannot grow past TextEdit's 16-bit length field. When it
       would, rebuild from history (which keeps the newest messages) first. */
    leadLen = ((*sDisplayTE)->teLength > 0) ? 1 : 0;
    if (!sRefreshing && (*sDisplayTE)->teLength + leadLen + prefixLen + msgLen + 1 > 30000) {
        RefreshConversationDisplay();
        leadLen = ((*sDisplayTE)->teLength > 0) ? 1 : 0;
    }

    /* One blank line between turns */
    if (leadLen > 0) {
        AppendDisplayText("\r", 1);
    }

    start = (*sDisplayTE)->teLength;
    AppendDisplayText(formattedMsg, (short)(prefixLen + msgLen + 1));

    /* The turn label is bold; the body stays plain. */
    if (isUserMessage && prefixLen > 0) {
        TextStyle style;

        style.tsFont  = kFontMonaco;
        style.tsFace  = bold;
        style.tsSize  = kChatBodySize;
        style.tsColor.red   = 0; /* tsColor is an RGBColor, not a ColorIndex */
        style.tsColor.green = 0;
        style.tsColor.blue  = 0;

        TESetSelect(start, (short)(start + prefixLen), sDisplayTE);
        TESetStyle(doAll, &style, false, sDisplayTE);
        TESetSelect((short)(start + prefixLen), (short)(start + prefixLen), sDisplayTE);
        TEDeactivate(sDisplayTE);
    }

    /* The model can change by adding a message, so refresh the status line */
    Chat_InvalidateHeader();

    isAdding = false;
}

/* Append text to the end of the display record, bounded by TextEdit's 16-bit
   length field. The caller is responsible for rebuilding from history when a
   whole message no longer fits. */
static void AppendDisplayText(const char *text, short len)
{
    short teLen, room;

    if (sDisplayTE == NULL || *sDisplayTE == NULL || text == NULL || len <= 0) {
        return;
    }

    teLen = (*sDisplayTE)->teLength;
    room  = (short)(30000 - teLen);
    if (room <= 0) {
        return;
    }
    if (len > room) {
        len = room;
    }

    TESetSelect(teLen, teLen, sDisplayTE);
    TEInsert(text, len, sDisplayTE);

    /* The display is a transcript, never an editing target while streaming */
    TEDeactivate(sDisplayTE);

    UpdateTextScrollbar(sDisplayTE, sScrollBar, true);
    InvalRect(&sDisplayRect);
}

/* Function to toggle between AI models */
void ChatWindow_ToggleAIModel(void)
{
    const char *modelMsg;

    if (!sInitialized || !sIsVisible) {
        return;
    }

    /* Toggle the model */
    if (gActiveAIModel == kMarkovModel) {
        SetActiveAIModel(kRemoteModel);
    }
    else if (gActiveAIModel == kRemoteModel) {
        SetActiveAIModel(kTemplateModel);
    }
    else {
        SetActiveAIModel(kMarkovModel);
    }

    /* Inform the user about the change */
    switch (gActiveAIModel) {
    case kMarkovModel:
        modelMsg = "Switched to Markov chain model.";
        break;
    case kRemoteModel:
        modelMsg = "Switched to remote AI model.";
        break;
    default:
        modelMsg = "Switched to Template-based model.";
        break;
    }

    /* Add message to chat window */
    FormatAndAddMessage(modelMsg, false);
}

/* Refresh the conversation history display */
static void RefreshConversationDisplay(void)
{
    short i, idx;
    short maxMessages = gConversationHistory.count;

    /* Prevent recursive refreshes */
    if (sRefreshing || !sInitialized || sDisplayTE == NULL || *sDisplayTE == NULL) {
        return;
    }

    sRefreshing = true;

    /* Use clipping to reduce flicker */
    GrafPtr savePort;
    Rect saveClip;
    GetPort(&savePort);
    SetPort(sWindow);
    saveClip = sWindow->clipRgn[0]->rgnBBox;
    ClipRect(&sDisplayRect);

    /* Clear the text display */
    TESetText("", 0, sDisplayTE);

    /* Build up text content, newest messages last */
    for (i = 0; i < maxMessages; i++) {
        /* Calculate index using circular buffer logic */
        idx = (gConversationHistory.head + i) % kMaxConversationHistory;

        ConversationMessage *msg = &gConversationHistory.messages[idx];
        if (msg->text[0] == '\0') {
            continue;
        }

        /* Stop before TextEdit's 16-bit length field overflows */
        if ((*sDisplayTE)->teLength > 28000) {
            AppendDisplayText("\r[history truncated]\r", 21);
            break;
        }

        /* Add message to display */
        FormatAndAddMessage(msg->text, (msg->type == kUserMessage));
    }

    /* Ensure display is deactivated */
    TEDeactivate(sDisplayTE);

    /* Restore clipping and trigger redraw */
    ClipRect(&saveClip);
    InvalRect(&sDisplayRect);
    SetPort(savePort);

    sRefreshing = false;
}

/* Send a message from the chat input field */
void ChatWindow_SendMessage(void)
{
    char promptBuffer[kMaxPromptLength];
    short promptLen;

    if (!sInitialized || sWindow == NULL || sInputTE == NULL || sDisplayTE == NULL) {
        return;
    }

    /* One request at a time */
    if (sAwaiting) {
        SysBeep(1);
        return;
    }

    /* Get the user's prompt */
    promptLen = (*sInputTE)->teLength;
    if (promptLen <= 0) {
        return; /* Empty prompt */
    }

    /* Copy text safely */
    if (promptLen >= kMaxPromptLength) {
        promptLen = kMaxPromptLength - 1;
    }

    memset(promptBuffer, 0, kMaxPromptLength);
    if ((*sInputTE)->hText == NULL || *(*sInputTE)->hText == NULL) {
        return;
    }

    BlockMoveData(*(*sInputTE)->hText, promptBuffer, promptLen);
    promptBuffer[promptLen] = '\0';

    /* Trim leading and trailing whitespace */
    TrimWhitespace(promptBuffer);

    /* Check if the message is now empty after trimming */
    if (promptBuffer[0] == '\0') {
        return;
    }

    /* Clear input field */
    ClearChatInput();

    /* Add user prompt to conversation history */
    AddUserPrompt(promptBuffer);

    /* Add the user message to display */
    FormatAndAddMessage(promptBuffer, true);

    /* The Models menu picks which engine answers. Markov and Template are the
       offline models: they need no network and no API key, and they are what a
       Mac without Open Transport or without a key can still use. Remote AI is
       the streaming HTTPS client. */
    if (gActiveAIModel != kRemoteModel) {
        char *response = GenerateAIResponse(&gConversationHistory);

        if (response == NULL || response[0] == '\0') {
            response = "Error: the local model returned nothing";
        }

        AppendDisplayText("\r", 1);
        AppendDisplayText("AI: ", 4);
        AppendDisplayText(response, (short)strlen(response));
        AppendDisplayText("\r", 1);
        AddAIResponse(response);
        return;
    }

    /* Start the request before writing into the transcript so a failure can be
       reported in place of the response body */
    if (!API_BeginRequest()) {
        const char *err = API_LastError();

        if (err == NULL) {
            err = "request failed";
        }

        AppendDisplayText("\r", 1);
        AppendDisplayText("AI: ", 4);
        AppendDisplayText(err, (short)strlen(err));
        AppendDisplayText("\r", 1);
        return;
    }

    /* Remember where the response starts so it can be cancelled cleanly,
       including the blank line that separates it from the user's turn */
    sResponseStart     = (*sDisplayTE)->teLength;
    sResponseLength    = 0;
    sResponseBuffer[0] = '\0';

    if (sResponseStart > 0) {
        AppendDisplayText("\r", 1);
    }
    AppendDisplayText("AI: ", 4);
    sAwaiting = true;
    Chat_InvalidateHeader();
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
    GrafPtr savePort;

    if (sWindow == NULL) {
        return;
    }

    /* Updates must always be balanced, even when the window is hidden */
    if (event->what == updateEvt) {
        if ((WindowPtr)event->message != sWindow) {
            return;
        }
        if (sIsVisible) {
            ChatWindow_Update();
        }
        else {
            GetPort(&savePort);
            SetPort(sWindow);
            BeginUpdate(sWindow);
            EndUpdate(sWindow);
            SetPort(savePort);
        }
        return;
    }

    if (!sIsVisible) {
        return;
    }

    GetPort(&savePort);
    SetPort(sWindow);

    switch (event->what) {
    case keyDown:
    case autoKey: {
        char key            = event->message & charCodeMask;
        Boolean isShiftDown = (event->modifiers & shiftKey) != 0;
        Boolean isCmdDown   = (event->modifiers & cmdKey) != 0;
        ChatWindow_HandleKeyDown(key, isShiftDown, isCmdDown);
        break;
    }

    case mouseDown: {
        WindowRef window;
        Point mousePt;

        /* Only clicks that really land in this window belong to us */
        if (FindWindow(event->where, &window) == inContent && window == sWindow) {
            mousePt = event->where;
            GlobalToLocal(&mousePt);
            ChatWindow_HandleContentClick(mousePt);
        }
        break;
    }

    case activateEvt:
        if ((WindowPtr)event->message == sWindow) {
            ChatWindow_HandleActivate((event->modifiers & activeFlag) != 0);
        }
        break;
    }

    SetPort(savePort);
}

/* Show or hide the chat window */
Boolean ChatWindow_Show(Boolean visible)
{
    if (!sInitialized || sWindow == NULL) {
        return false;
    }

    if (visible) {
        /* Refresh conversation display unless a response is streaming */
        if (!sAwaiting) {
            RefreshConversationDisplay();
        }

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
        /* Abandon an in-flight request rather than leaving it running unseen */
        if (sAwaiting) {
            CancelResponse();
        }

        HideWindow(sWindow);
        sIsVisible = false;

        /* Reset chat history */
        InitModels();
    }

    return true;
}

/* Get the window reference for the chat window */
WindowRef ChatWindow_GetWindowRef(void)
{
    return sWindow;
}

/* Private helper functions for menu integration */
static Boolean IsDisplayActive(void)
{
    if (sDisplayTE == NULL || *sDisplayTE == NULL || sFocusTE != sDisplayTE)
        return false;

    /* Check if display has a selection */
    return ((*sDisplayTE)->selStart < (*sDisplayTE)->selEnd);
}

static Boolean IsInputActive(void)
{
    if (sInputTE == NULL || *sInputTE == NULL || sFocusTE != sInputTE)
        return false;

    return true;
}

/* Get the active TEHandle for text operations */
TEHandle ChatWindow_GetActiveTE(void)
{
    /* Prioritize display if it has a selection (for copy operations) */
    if (IsDisplayActive())
        return sDisplayTE;

    /* Otherwise return the focused field, or the input field */
    if (sFocusTE == sDisplayTE && sDisplayTE != NULL)
        return sDisplayTE;
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

/* Abandon the in-flight request and remove its partial output */
static void CancelResponse(void)
{
    short teLen;

    if (!sAwaiting) {
        return;
    }

    API_Cancel();

    sAwaiting          = false;
    sResponseLength    = 0;
    sResponseBuffer[0] = '\0';
    Chat_InvalidateHeader();

    if (sDisplayTE == NULL || *sDisplayTE == NULL) {
        return;
    }

    teLen = (*sDisplayTE)->teLength;
    if (sResponseStart >= 0 && sResponseStart <= teLen) {
        TESetSelect(sResponseStart, teLen, sDisplayTE);
        TEDelete(sDisplayTE);
        UpdateTextScrollbar(sDisplayTE, sScrollBar, true);
        InvalRect(&sDisplayRect);
    }
}

/* Perform idle processing: drive the streaming request */
void ChatWindow_Idle(void)
{
    char chunk[128];
    short received;
    APIState state;

    if (!sInitialized || sWindow == NULL || !sIsVisible) {
        return;
    }

    /* Cursor blinking for the field that owns the insertion point */
    if (sFocusTE != NULL) {
        TEIdle(sFocusTE);
    }
    else if (sInputTE != NULL) {
        TEIdle(sInputTE);
    }

    if (!sAwaiting) {
        return;
    }

    /* Drain everything the client has decoded since the last poll */
    while ((received = API_Poll(chunk, (short)sizeof(chunk))) > 0) {
        short room = (short)(kMaxResponseLength - sResponseLength);
        short copy = received;

        if (copy > room) {
            copy = room;
        }

        if (copy > 0) {
            memcpy(sResponseBuffer + sResponseLength, chunk, (size_t)copy);
            sResponseLength += copy;
            sResponseBuffer[sResponseLength] = '\0';
            AppendDisplayText(chunk, copy);
        }
    }

    state = API_State();

    if (state == kAPIStateDone) {
        AppendDisplayText("\r", 1);
        if (sResponseLength > 0) {
            AddAIResponse(sResponseBuffer);
        }
        sAwaiting          = false;
        sResponseLength    = 0;
        sResponseBuffer[0] = '\0';
        Chat_InvalidateHeader();
    }
    else if (state == kAPIStateError) {
        const char *err = API_LastError();

        if (err == NULL) {
            err = "request failed";
        }

        AppendDisplayText("\rError: ", 8);
        AppendDisplayText(err, (short)strlen(err));
        AppendDisplayText("\r", 1);

        sAwaiting          = false;
        sResponseLength    = 0;
        sResponseBuffer[0] = '\0';
        Chat_InvalidateHeader();
    }
}

/* ---------------------------------------------------------------- */
/* Test hooks (see src/selftest.c)                                   */
/* ---------------------------------------------------------------- */

/* Put text in the input field and press Return, through the same path the
   user's keystrokes take. */
Boolean ChatWindow_SubmitForTest(const char *prompt)
{
    if (prompt == NULL || sInputTE == NULL || *sInputTE == NULL) {
        return false;
    }

    TESetText((Ptr)prompt, (long)strlen(prompt), sInputTE);
    ChatWindow_SendMessage();
    return true;
}

/* Copy the transcript out, bounded. */
short ChatWindow_TranscriptForTest(char *out, short max)
{
    short len;

    if (out == NULL || max <= 0) {
        return 0;
    }
    out[0] = '\0';

    if (sDisplayTE == NULL || *sDisplayTE == NULL || (*sDisplayTE)->hText == NULL) {
        return 0;
    }

    len = (*sDisplayTE)->teLength;
    if (len > (short)(max - 1)) {
        len = (short)(max - 1);
    }
    if (len > 0) {
        BlockMoveData(*(*sDisplayTE)->hText, out, (Size)len);
    }
    out[len] = '\0';
    return len;
}
