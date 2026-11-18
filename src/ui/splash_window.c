#include <ControlDefinitions.h>
#include <Controls.h>
#include <Events.h>
#include <Fonts.h>
#include <Memory.h>
#include <Quickdraw.h>
#include <Windows.h>

#include "../constants.h"
#include "../error.h"
#include "splash_window.h"
#include "utils.h"
#include "window_manager.h"

/* Version string is supplied by CMake; plain source builds report "dev". */
#ifndef APP_VERSION_STR
#define APP_VERSION_STR "dev"
#endif

/* Splash layout, window-local pixels. This is the only place these numbers live. */
enum {
    kSplashNameFont     = 1,  /* application font: Chicago */
    kSplashMarkFontSize = 36, /* "AI" inside the brand block */
    kSplashNameSize     = 12,
    kSplashSmallSize    = 9,  /* tagline and version */
    kSplashMarkSize     = 120, /* the brand block is square */
    kSplashLockupText   = 62,  /* name gap + tagline gap + small print below the mark */
    kSplashMinTop       = 24,  /* never crowd the title bar */
    kSplashNameGap      = 30, /* brand block bottom to the app-name baseline */
    kSplashTaglineGap   = 20, /* app-name baseline to the tagline baseline */
    kSplashButtonWidth  = 120,
    kSplashButtonHeight = 24,
    kSplashButtonBottom = 72, /* window bottom to the button bottom */
    kSplashDefaultRing  = 3,  /* ring offset for the default button */
    kSplashEdgeInset    = 16  /* version string inset from the window edges */
};

/* Private module variables */
static WindowRef sWindow              = NULL;
static ControlHandle sStartChatButton = NULL;
static Boolean sInitialized           = false;
static Boolean sIsVisible             = false;

/* The Start Chat control rectangle, derived from the window every time so the
   layout constants above stay authoritative. */
static void Splash_ButtonRect(const Rect *frameRect, Rect *buttonRect)
{
    short centre = (frameRect->left + frameRect->right) / 2;
    short top    = frameRect->bottom - kSplashButtonBottom;

    SetRect(buttonRect, centre - kSplashButtonWidth / 2, top,
            centre + kSplashButtonWidth / 2, top + kSplashButtonHeight);
}

/* Draw the splash window UI elements.
 *
 * One lockup: brand block, app name, tagline, then the version in small grey
 * print. Port, pen, pattern, colour and text state are all restored. */
static void DrawSplashWindowUI(WindowPtr window)
{
    Rect frameRect, markRect, buttonRect;
    GrafPtr savePort;
    PenState savePen;
    Pattern saveBackPat;
    RGBColor saveFore, saveBack;
    FontInfo fontInfo;
    Str255 version;
    RGBColor smallPrint = {0x4444, 0x4444, 0x4444};
    short saveFont, saveSize, saveFace;
    short markLeft, markTop, baseline, width;
    short lockupSpace;

    if (window == NULL) {
        return;
    }

    GetPort(&savePort);
    SetPort(window);
    GetPenState(&savePen);
    GetForeColor(&saveFore);
    GetBackColor(&saveBack);
    { GrafPtr p; GetPort(&p); saveBackPat = p->bkPat; }
    saveFont = window->txFont;
    saveSize = window->txSize;
    saveFace = window->txFace;

    frameRect = window->portRect;

    /* Flat chrome. A 1-bit pattern dithers into visible stripes on a colour
     * display and swallowed the title, so tint the background colour and fill
     * with an all-ones pattern: solid on colour, white on a 1-bit screen. */
    {
        static const RGBColor chrome = {0xCCCC, 0xCCCC, 0xCCCC};

        RGBBackColor(&chrome);
    }
    BackPat(&qd.white);
    EraseRect(&frameRect);

    /* Brand block: solid black square with the mark knocked out in white.
       The lockup is centred in the space above the Start Chat button. */
    markLeft    = (frameRect.left + frameRect.right - kSplashMarkSize) / 2;
    lockupSpace = frameRect.bottom - kSplashButtonBottom;
    markTop     = (lockupSpace - kSplashMarkSize - kSplashLockupText) / 2;
    if (markTop < frameRect.top + kSplashMinTop) {
        markTop = frameRect.top + kSplashMinTop;
    }
    SetRect(&markRect, markLeft, markTop, markLeft + kSplashMarkSize, markTop + kSplashMarkSize);

    PenPat(&qd.black);
    PenSize(1, 1);
    ForeColor(blackColor);
    PaintRect(&markRect);

    TextFont(kFontMonaco);
    TextSize(kSplashMarkFontSize);
    TextFace(bold);
    ForeColor(whiteColor);
    GetFontInfo(&fontInfo);
    width = StringWidth("\pAI");
    MoveTo(markRect.left + (kSplashMarkSize - width) / 2,
           markRect.top + (kSplashMarkSize - (fontInfo.ascent + fontInfo.descent)) / 2 +
               fontInfo.ascent);
    DrawString("\pAI");

    /* App name and one-line tagline, centred on the mark. */
    TextFont(kSplashNameFont);
    TextSize(kSplashNameSize);
    TextFace(bold);
    ForeColor(blackColor);
    baseline = markRect.bottom + kSplashNameGap;
    width    = StringWidth("\pMacintosh AI");
    MoveTo((frameRect.left + frameRect.right - width) / 2, baseline);
    DrawString("\pMacintosh AI");

    TextFont(kFontGeneva);
    TextSize(kSplashSmallSize);
    TextFace(normal);
    baseline += kSplashTaglineGap;
    width = StringWidth("\pAI chat for the Macintosh");
    MoveTo((frameRect.left + frameRect.right - width) / 2, baseline);
    DrawString("\pAI chat for the Macintosh");

    /* Start Chat is the default action: a black ring three pixels outside the
       control's own outline reads as the double ring. The control draws on top. */
    Splash_ButtonRect(&frameRect, &buttonRect);
    InsetRect(&buttonRect, -kSplashDefaultRing, -kSplashDefaultRing);
    PenPat(&qd.black);
    PenSize(1, 1);
    FrameRect(&buttonRect);

    /* Version, bottom right, small and grey. */
    CStrToPStr(APP_VERSION_STR, version);
    TextFont(kFontGeneva);
    TextSize(kSplashSmallSize);
    TextFace(normal);
    RGBForeColor(&smallPrint);
    width = StringWidth(version);
    MoveTo(frameRect.right - kSplashEdgeInset - width, frameRect.bottom - kSplashEdgeInset);
    DrawString(version);

    SetPenState(&savePen);
    RGBForeColor(&saveFore);
    RGBBackColor(&saveBack);
    BackPat(&saveBackPat);
    TextFont(saveFont);
    TextSize(saveSize);
    TextFace(saveFace);
    SetPort(savePort);
}

/* Initialize and create the splash window */
Boolean SplashWindow_Initialize(void)
{
    Rect screenRect, windowRect, buttonRect;
    short screenWidth, screenHeight;
    short windowWidth, windowHeight;
    OSErr err;

    /* Prevent double initialization */
    if (sInitialized) {
        return true;
    }

    /* Check available memory */
    err = CheckMemory();
    if (err != noErr) {
        HandleError(kErrMemoryFull, kCtxCreatingMainWindow, false); /* Non-fatal */
        return false;
    }

    /* Calculate screen dimensions */
    screenRect   = qd.screenBits.bounds;
    screenWidth  = screenRect.right - screenRect.left;
    screenHeight = screenRect.bottom - screenRect.top;

    /* Make the window about 75% of screen width/height */
    windowWidth  = (screenWidth * 3) / 4;
    windowHeight = (screenHeight * 3) / 4;

    /* Create centered window rectangle */
    SetRect(&windowRect, (screenWidth - windowWidth) / 2, (screenHeight - windowHeight) / 2,
            (screenWidth - windowWidth) / 2 + windowWidth,
            (screenHeight - windowHeight) / 2 + windowHeight);

    /* Create the window directly like the chat window does */
    sWindow = NewWindow(NULL, &windowRect, "\pMacintosh AI", true, documentProc, (WindowPtr)-1,
                        true, 0);

    if (sWindow == NULL) {
        HandleError(kErrWindowCreation, kCtxCreatingMainWindow, false); /* Non-fatal */
        return false;
    }

    /* Make window visible and active */
    ShowWindow(sWindow);
    SelectWindow(sWindow);

    /* Set port for drawing */
    SetPort(sWindow);

    /* Draw all the UI elements */
    DrawSplashWindowUI(sWindow);

    /* Create "Start Chat" button: the default action, bottom centre */
    Splash_ButtonRect(&sWindow->portRect, &buttonRect);

    /* Use proper button proc for Macintosh (pushButProc without outline for better look) */
    sStartChatButton = NewControl(sWindow, &buttonRect, "\pStart Chat", true, 0, 0, 1, pushButProc,
                                  (long)kStartChatBtn);

    if (sStartChatButton == NULL) {
        HandleError(kErrControlCreation, kCtxCreatingButton, false); /* Non-fatal */
        /* We can continue without the button - user can still use Cmd-L */
    }

    /* Force a redraw of the entire window */
    InvalRect(&sWindow->portRect);

    sInitialized = true;

    return true;
}

/* Dispose of the splash window and clean up resources */
void SplashWindow_Dispose(void)
{
    if (!sInitialized) {
        return;
    }

    /* Dispose controls */
    if (sStartChatButton != NULL) {
        DisposeControl(sStartChatButton);
        sStartChatButton = NULL;
    }

    /* Dispose window after all resources are freed */
    if (sWindow != NULL) {
        DisposeWindow(sWindow);
        sWindow = NULL;
    }

    sInitialized = false;
}

/* Render the splash window contents */
void SplashWindow_Render(void)
{
    GrafPtr savePort;

    if (!sInitialized || sWindow == NULL) {
        return;
    }

    GetPort(&savePort);
    SetPort(sWindow);

    /* Draw the UI elements using the shared function */
    DrawSplashWindowUI(sWindow);

    /* Make sure the button is properly drawn */
    if (sStartChatButton != NULL) {
        DrawControls(sWindow);
    }

    SetPort(savePort);
}

/* Handle update events for the splash window */
static void SplashWindow_Update(void)
{
    if (!sInitialized || sWindow == NULL) {
        return;
    }

    SetPort(sWindow);
    BeginUpdate(sWindow);

    /* Call the render function to draw window contents */
    SplashWindow_Render();

    EndUpdate(sWindow);
}

/* Handle mouse clicks in the content area of the splash window */
Boolean SplashWindow_HandleContentClick(Point localPt)
{
    ControlHandle control;
    short part;

    if (!sInitialized || sWindow == NULL) {
        return false;
    }

    /* Find which control was clicked */
    part = FindControl(localPt, sWindow, &control);

    /* Track the button so it highlights until the mouse is released; only a
       release inside the button counts as a click. */
    if (part && control == sStartChatButton) {
        return TrackControl(control, localPt, NULL) != 0;
    }

    return false; /* No button click handled */
}

/* Handle all events for the splash window */
void SplashWindow_HandleEvent(EventRecord *event)
{
    GrafPtr savePort;

    if (sWindow == NULL) {
        return;
    }

    if (!sIsVisible) {
        return;
    }

    GetPort(&savePort);
    SetPort(sWindow);

    switch (event->what) {
    case mouseDown: {
        WindowRef window;
        Point mousePt;

        /* Only clicks that really land in this window belong to us */
        if (FindWindow(event->where, &window) == inContent && window == sWindow) {
            mousePt = event->where;
            GlobalToLocal(&mousePt);
            if (SplashWindow_HandleContentClick(mousePt)) {
                /* If the start chat button was clicked, open the chat window */
                if (WindowManager_OpenWindow(kWindowTypeChat)) {
                    WindowManager_CloseWindow(kWindowTypeSplash);
                }
                else {
                    HandleError(kErrWindowCreation, kCtxOpeningChatWindow, false);
                }
            }
        }
    } break;

    case updateEvt:
        if ((WindowPtr)event->message == sWindow) {
            SplashWindow_Update();
        }
        break;

    default:
        /* Splash window doesn't handle other event types */
        break;
    }

    SetPort(savePort);
}

/* Get the window reference for the splash window */
WindowRef SplashWindow_GetWindowRef(void)
{
    return sWindow;
}

/* Show or hide the splash window */
Boolean SplashWindow_Show(Boolean visible)
{
    if (!sInitialized || sWindow == NULL) {
        return false;
    }

    if (visible) {
        /* Set port before showing window */
        SetPort(sWindow);

        /* Show and select the window */
        ShowWindow(sWindow);
        SelectWindow(sWindow);

        /* Call render to draw all content */
        SplashWindow_Render();

        /* Update visibility tracking */
        sIsVisible = true;
    }
    else {
        HideWindow(sWindow);
        sIsVisible = false;
    }

    return true;
}

/* Determine if the window is visible */
Boolean SplashWindow_IsVisible(void)
{
    return sIsVisible;
}