#include <Controls.h>
#include <Fonts.h>
#include <Memory.h>
#include <Menus.h>
#include <Quickdraw.h>
#include <Resources.h>
#include <Windows.h>

#include "../constants.h"
#include "../error.h"
#include "splash_window.h"

/* Private module variables */
static WindowRef sWindow              = NULL;
static ControlHandle sStartChatButton = NULL;
static Boolean sInitialized           = false;

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

/* Initialize and create the splash window */
void SplashWindow_Initialize(void)
{
    Rect buttonRect, frameRect, iconRect;
    Pattern theBits;
    short i, j, patternHeight;
    OSErr err;

    /* Prevent double initialization */
    if (sInitialized) {
        return;
    }

    /* Check memory before creating window */
    err = CheckMemory();
    if (err != noErr) {
        HandleError(kErrMemoryFull, kCtxCreatingMainWindow, false); /* Non-fatal for this window */
        /* Cannot continue without memory, but let the caller handle exit */
        return;
    }

    /* Create main window */
    sWindow = GetNewWindow(kMainWindowID, NULL, (WindowPtr)-1);
    if (sWindow == NULL) {
        HandleError(kErrWindowCreation, kCtxCreatingMainWindow, false); /* Non-fatal */
        return;
    }

    SetPort(sWindow);

    /* Fill window with gradient pattern */
    frameRect     = sWindow->portRect;
    patternHeight = 80;

    /* Create visual gradient for background */
    for (i = 0; i < patternHeight; i++) {
        /* Vary the pattern from light to dark */
        short intensity = 255 - ((i * 192) / patternHeight);

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
    ForeColor(blackColor); /* Change from blue to black for better visibility */

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

    /* Draw glowing effect around AI text */
    PenSize(1, 1);

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

    /* Create "Start Chat" button - standard Mac rounded button with proper style */
    SetRect(&buttonRect, (frameRect.right - 120) / 2, /* Center button */
            iconRect.bottom + 120,                    /* Below the text */
            (frameRect.right + 120) / 2, iconRect.bottom + 145);

    /* Use proper button proc for Macintosh (pushButProc without outline for better look) */
    sStartChatButton = NewControl(sWindow, &buttonRect, "\pStart Chat", true, 0, 0, 1, pushButProc,
                                  (long)kStartChatBtn);

    if (sStartChatButton == NULL) {
        HandleError(kErrControlCreation, kCtxCreatingButton, false); /* Non-fatal */
        /* We can continue without the button - user can still use Cmd-L */
    }

    sInitialized = true;
}

/* Dispose of the splash window and clean up resources */
void SplashWindow_Dispose(void)
{
    if (!sInitialized) {
        return;
    }

    if (sStartChatButton != NULL) {
        DisposeControl(sStartChatButton);
        sStartChatButton = NULL;
    }

    if (sWindow != NULL) {
        DisposeWindow(sWindow);
        sWindow = NULL;
    }

    sInitialized = false;
}

/* Handle update events for the splash window */
void SplashWindow_Update(void)
{
    Rect frameRect, iconRect;
    Pattern theBits;
    short i, j, patternHeight;

    if (!sInitialized || sWindow == NULL) {
        return;
    }

    SetPort(sWindow);
    BeginUpdate(sWindow);

    frameRect     = sWindow->portRect;
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
        ShowWindow(sWindow);
        SelectWindow(sWindow);
        InvalRect(&sWindow->portRect);
    }
    else {
        HideWindow(sWindow);
    }
}

/* Determine if the window is visible */
Boolean SplashWindow_IsVisible(void)
{
    if (!sInitialized || sWindow == NULL) {
        return false;
    }

    return ((WindowPeek)sWindow)->visible;
}