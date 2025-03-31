#include <Controls.h>
#include <Events.h>
#include <Fonts.h>
#include <Memory.h>
#include <Menus.h>
#include <Quickdraw.h>
#include <Resources.h>
#include <Windows.h>

#include "../constants.h"
#include "../error.h"
#include "splash_window.h"
#include "window_manager.h"

/* Private module variables */
static WindowRef sWindow              = NULL;
static ControlHandle sStartChatButton = NULL;
static Boolean sInitialized           = false;
static Boolean sIsVisible             = false;

/* Initialize a pattern */
static void PatternInit(Pattern *pattern, unsigned char b1, unsigned char b2, unsigned char b3,
                        unsigned char b4, unsigned char b5, unsigned char b6, unsigned char b7,
                        unsigned char b8)
{
    pattern->pat[0] = b1;
    pattern->pat[1] = b2;
    pattern->pat[2] = b3;
    pattern->pat[3] = b4;
    pattern->pat[4] = b5;
    pattern->pat[5] = b6;
    pattern->pat[6] = b7;
    pattern->pat[7] = b8;
}

/* Draw the splash window UI elements */
static void DrawSplashWindowUI(WindowPtr window)
{
    Rect frameRect, iconRect;
    Pattern theBits;
    short i, j, patternHeight;

    /* Get window dimensions */
    frameRect     = window->portRect;
    patternHeight = 80;

    /* Create visual gradient for background */
    for (i = 0; i < patternHeight; i++) {
        /* Create a pattern for this row */
        for (j = 0; j < 8; j++) {
            theBits.pat[j] = (i % 4 == 0) ? 0xAA : ((i % 4 == 1) ? 0x55 : 0);
        }

        /* Set the pattern and draw a line */
        PenPat(&theBits);
        MoveTo(frameRect.left, frameRect.top + i);
        LineTo(frameRect.right, frameRect.top + i);
    }

    /* Fill rest of window with solid color */
    PenNormal();
    ForeColor(blackColor);
    BackColor(whiteColor);

    /* Draw a computer screen with text "AI for Macintosh" */
    SetRect(&iconRect, (frameRect.right - 100) / 2, frameRect.top + 30, (frameRect.right + 100) / 2,
            frameRect.top + 100);

    /* Draw computer outline */
    PenSize(2, 2);
    FrameRoundRect(&iconRect, 8, 8);

    /* Draw computer screen area with a different pattern background */
    InsetRect(&iconRect, 8, 8);

    /* Fill with solid white background as requested */
    BackColor(whiteColor);
    EraseRoundRect(&iconRect, 4, 4); /* Erase to white */

    /* Draw frame */
    PenNormal();
    FrameRoundRect(&iconRect, 4, 4);

    /* Draw "AI" text in futuristic style */
    TextFont(kFontMonaco);
    TextSize(36); /* Bigger text size */
    TextFace(bold);
    ForeColor(blackColor); /* Use black for better visibility */

    /* Center the text in the screen - both horizontally and vertically */
    /* For vertical centering, we need to account for text ascent and add a few pixels right */
    short aiTextWidth  = StringWidth("\pAI");
    short aiTextHeight = 30; /* Approximate height for this font size */
    MoveTo(iconRect.left + (iconRect.right - iconRect.left - aiTextWidth) / 2 +
               5, /* Add 5 pixels to the right */
           iconRect.top + (iconRect.bottom - iconRect.top - aiTextHeight) / 2 + aiTextHeight);
    DrawString("\pAI");

    /* Reset text style for rest of drawing */
    TextFont(kFontGeneva);
    TextSize(18);
    TextFace(bold);
    ForeColor(blackColor);

    /* Add more space after the box */
    short titleSpacing = 16; /* Requested 16px spacing */

    /* Calculate vertical spacing for better balance */
    short availableSpace =
        75 - 25 -
        titleSpacing; /* Space between iconRect.bottom and the decorative line, minus new spacing */
    short textHeight = 18 * 2 + 5; /* Height of two lines of text with 5px spacing */
    short topMargin  = (availableSpace - textHeight) / 2; /* Center vertically */

    /* Draw "for" with Macintosh font - properly spaced, with additional spacing */
    MoveTo((frameRect.right - StringWidth("\pfor")) / 2,
           iconRect.bottom + titleSpacing + topMargin + 18);
    DrawString("\pfor");

    /* Draw "Macintosh" with classic Mac font */
    TextFont(kFontGeneva);
    TextSize(18);
    TextFace(bold);
    /* Add more spacing between for and Macintosh */
    MoveTo((frameRect.right - StringWidth("\pMacintosh")) / 2,
           iconRect.bottom + titleSpacing + topMargin + 18 + 15 + 18);
    DrawString("\pMacintosh");

    /* Add decorative lines */
    PenSize(1, 1);
    PenPat(&qd.gray);
    MoveTo(frameRect.left + 20, iconRect.bottom + 75);
    LineTo(frameRect.right - 20, iconRect.bottom + 75);
    PenNormal();

    /* Draw subtitle */
    TextFont(kFontGeneva);
    TextSize(12);
    TextFace(normal);
}

/* Initialize and create the splash window */
void SplashWindow_Initialize(void)
{
    Rect screenRect, windowRect, frameRect, buttonRect;
    short screenWidth, screenHeight;
    short windowWidth, windowHeight;
    OSErr err;

    /* Prevent double initialization */
    if (sInitialized) {
        return;
    }

    /* Check available memory */
    err = CheckMemory();
    if (err != noErr) {
        HandleError(kErrMemoryFull, kCtxCreatingMainWindow, false); /* Non-fatal */
        return;
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
    sWindow = NewWindow(NULL, &windowRect, "\pAI for Macintosh", true, documentProc, (WindowPtr)-1,
                        true, 0);

    if (sWindow == NULL) {
        HandleError(kErrWindowCreation, kCtxCreatingMainWindow, false); /* Non-fatal */
        return;
    }

    /* Make window visible and active */
    ShowWindow(sWindow);
    SelectWindow(sWindow);

    /* Set port for drawing */
    SetPort(sWindow);

    /* Get window dimensions for drawing */
    frameRect = sWindow->portRect;

    /* Draw all the UI elements */
    DrawSplashWindowUI(sWindow);

    /* Create "Start Chat" button - standard Mac rounded button with proper style */
    SetRect(&buttonRect, (frameRect.right - 120) / 2, /* Center button */
            frameRect.top + 190,                      /* Position below the text (moved up 40px) */
            (frameRect.right + 120) / 2, frameRect.top + 215);

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

/* Handle update events for the splash window */
void SplashWindow_Update(void)
{
    if (!sInitialized || sWindow == NULL) {
        return;
    }

    SetPort(sWindow);
    BeginUpdate(sWindow);

    /* Draw the UI elements using the shared function */
    DrawSplashWindowUI(sWindow);

    /* Make sure the button is properly drawn */
    if (sStartChatButton != NULL) {
        DrawControls(sWindow);
    }

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

    /* If Start Chat button was clicked */
    if (part && control == sStartChatButton) {
        /* Visual feedback */
        HiliteControl(control, 1); /* Highlight button */
        Delay(8, NULL);            /* Short delay */
        HiliteControl(control, 0); /* Return to normal */

        return true; /* Indicate button was clicked */
    }

    return false; /* No button click handled */
}

/* Handle all events for the splash window */
void SplashWindow_HandleEvent(EventRecord *event)
{
    if (event->what == mouseDown) {
        Point mousePt = event->where;
        GlobalToLocal(&mousePt);
        if (SplashWindow_HandleContentClick(mousePt)) {
            /* If the start chat button was clicked, open the chat window */
            WindowManager_OpenWindow(kWindowTypeChat);
            WindowManager_CloseWindow(kWindowTypeSplash);
        }
    }
    /* Splash window doesn't handle other event types */
}

/* Get the window reference for the splash window */
WindowRef SplashWindow_GetWindowRef(void)
{
    return sWindow;
}

/* Show or hide the splash window */
void SplashWindow_Show(Boolean visible)
{
    if (!sInitialized || sWindow == NULL) {
        return;
    }

    if (visible) {
        /* Draw all the UI elements to ensure they're shown correctly */
        SetPort(sWindow);
        DrawSplashWindowUI(sWindow);

        /* Make sure the button is visible */
        if (sStartChatButton != NULL) {
            DrawControls(sWindow);
        }

        /* Show and select the window */
        ShowWindow(sWindow);
        SelectWindow(sWindow);

        /* Force complete redraw */
        InvalRect(&sWindow->portRect);

        /* Update visibility tracking */
        sIsVisible = true;
    }
    else {
        HideWindow(sWindow);
        sIsVisible = false;
    }
}

/* Determine if the window is visible */
Boolean SplashWindow_IsVisible(void)
{
    return sIsVisible;
}