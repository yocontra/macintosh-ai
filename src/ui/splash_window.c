#include <Fonts.h>
#include <Memory.h>
#include <Quickdraw.h>
#include <Windows.h>
#include <stdlib.h>
#include <string.h>

#include "../constants.h"
#include "../error.h"
#include "../nuklear/nuklear.h" /* Include only header without implementation */
#include "splash_window.h"

/* External function declarations instead of including nuklear_quickdraw.h */
extern struct nk_context* nk_quickdraw_init(unsigned int width, unsigned int height);
extern int nk_quickdraw_handle_event(EventRecord *event, struct nk_context *nuklear_context);
extern void nk_quickdraw_render(WindowPtr window, struct nk_context *ctx);
extern void nk_quickdraw_shutdown(void);

/* External references to the shared nuklear context */
extern struct nk_context *ctx;

/* Private module variables */
static WindowRef sWindow      = NULL;
static Boolean sInitialized   = false;
static Boolean sIsVisible     = false;
static Boolean sButtonClicked = false;

/* Function declarations */
void nuklearApp_Splash(struct nk_context *ctx);
void refreshSplashApp(Boolean blankInput);

/* The main Nuklear UI function for the splash window
 * This follows the pattern from nuklear_calculator.c
 */
void nuklearApp_Splash(struct nk_context *ctx)
{
    /* Create a window that respects the window's title bar and border */
    if (nk_begin(ctx, "AI for Macintosh", nk_rect(0, 0, 340, 300), 
        NK_WINDOW_NO_SCROLLBAR|NK_WINDOW_BORDER|NK_WINDOW_TITLE|NK_WINDOW_MOVABLE)) {
        
        /* Get canvas for manual drawing */
        struct nk_rect bounds = nk_window_get_bounds(ctx);
        
        /* Define colors */
        RGBColor lightBlue = {0xAAAA, 0xAAAA, 0xFFFF}; /* Light blue */
        RGBColor mediumBlue = {0x8888, 0x8888, 0xEEEE}; /* Medium blue */
        RGBColor darkBlue = {0x6666, 0x6666, 0xDDDD};  /* Dark blue */
        RGBColor white = {0xFFFF, 0xFFFF, 0xFFFF};
        RGBColor black = {0, 0, 0};
        RGBColor lightGray = {0xDDDD, 0xDDDD, 0xDDDD};
        RGBColor darkGray = {0x3333, 0x3333, 0x3333};
        
        /* Save current graphics state */
        PenState savedPen;
        RGBColor savedForeColor, savedBackColor;
        GetPenState(&savedPen);
        GetForeColor(&savedForeColor);
        GetBackColor(&savedBackColor);
        
        /* Draw gradient background covering the whole top half of window */
        for (int i = 0; i < 10; i++) {
            Rect gradientRect;
            SetRect(&gradientRect, 
                    0, 
                    i * 15, 
                    (short)bounds.w, 
                    (i + 1) * 15);
            
            /* Calculate gradient color - transition from light to dark blue */
            RGBColor gradColor;
            gradColor.red = lightBlue.red - ((lightBlue.red - darkBlue.red) * i / 10);
            gradColor.green = lightBlue.green - ((lightBlue.green - darkBlue.green) * i / 10);
            gradColor.blue = lightBlue.blue - ((lightBlue.blue - darkBlue.blue) * i / 10);
            
            RGBForeColor(&gradColor);
            RGBBackColor(&gradColor);
            PaintRect(&gradientRect);
        }
        
        /* Draw Mac body - light gray rectangle with rounded corners - centered horizontally */
        Rect macBody;
        short macWidth = 180;
        short macHeight = 120;
        short macX = (bounds.w - macWidth) / 2;
        
        SetRect(&macBody, 
                macX, 
                20, 
                macX + macWidth, 
                20 + macHeight);
                
        RGBForeColor(&lightGray);
        RGBBackColor(&lightGray);
        PaintRoundRect(&macBody, 10, 10);
        
        /* Draw Mac screen - dark gray rectangle - centered in the body */
        Rect screen;
        short screenWidth = 140;
        short screenHeight = 80;
        short screenX = macX + (macWidth - screenWidth) / 2;
        short screenY = 20 + 10;
        
        SetRect(&screen, 
                screenX,
                screenY, 
                screenX + screenWidth, 
                screenY + screenHeight);
                
        RGBForeColor(&darkGray);
        RGBBackColor(&darkGray);
        PaintRect(&screen);
        
        /* Draw "AI" text on the screen - properly centered */
        TextFont(kFontIDMonaco);
        TextSize(36);
        TextFace(bold);
        RGBForeColor(&white);
        
        /* Calculate text position to center it */
        FontInfo fontInfo;
        GetFontInfo(&fontInfo);
        short textWidth = StringWidth("\pAI");
        short textX = screenX + (screenWidth - textWidth) / 2;
        short textY = screenY + (screenHeight + fontInfo.ascent - fontInfo.descent) / 2;
        
        MoveTo(textX, textY);
        DrawString("\pAI");
        
        /* Draw Mac base - centered under the body */
        Rect base;
        short baseWidth = 80;
        short baseX = macX + (macWidth - baseWidth) / 2;
        
        SetRect(&base, 
                baseX, 
                20 + macHeight, 
                baseX + baseWidth, 
                20 + macHeight + 10);
                
        RGBForeColor(&lightGray);
        RGBBackColor(&lightGray);
        PaintRect(&base);
        
        /* Restore graphics state */
        SetPenState(&savedPen);
        RGBForeColor(&savedForeColor);
        RGBBackColor(&savedBackColor);
        
        /* Need to reset font to system font */
        TextFont(systemFont);
        TextSize(0);
        TextFace(normal);

        /* Button positioned closer to the Mac drawing */
        nk_layout_row_dynamic(ctx, 10, 1);
        nk_spacing(ctx, 1);

        /* Center the button with specific width */
        const float buttonWidth = 120.0f;
        nk_layout_row_begin(ctx, NK_STATIC, 35, 1);
        nk_layout_row_push(ctx, (bounds.w - buttonWidth) / 2); /* Left margin to center */
        nk_spacing(ctx, 1);
        nk_layout_row_end(ctx);
        
        nk_layout_row_begin(ctx, NK_STATIC, 35, 1);
        nk_layout_row_push(ctx, buttonWidth); /* Button width */
        
        /* Draw the button and check if it's clicked */
        if (nk_button_label(ctx, "Start Chat")) {
            sButtonClicked = true;
        }
        
        nk_layout_row_end(ctx);

        nk_end(ctx);
    }
}

/* Refresh the splash app - follows pattern from nuklear_calculator.c */
void refreshSplashApp(Boolean blankInput)
{
    if (!sInitialized || !sIsVisible || sWindow == NULL)
        return;
        
    SetPort(sWindow);
    
    nk_input_begin(ctx);
    
    if (blankInput) {
        nk_input_key(ctx, NK_KEY_DEL, 1);
        nk_input_key(ctx, NK_KEY_DEL, 0);
    }
    
    nk_input_end(ctx);
    nuklearApp_Splash(ctx);
    nk_quickdraw_render(sWindow, ctx);
    nk_clear(ctx);
}

/* Initialize and create the splash window */
void SplashWindow_Initialize(void)
{
    Rect windowRect;
    short screenWidth, screenHeight;
    short windowWidth, windowHeight;
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

    /* Calculate window size (centered on screen) */
    screenWidth  = qd.screenBits.bounds.right - qd.screenBits.bounds.left;
    screenHeight = qd.screenBits.bounds.bottom - qd.screenBits.bounds.top;

    windowWidth  = 340; /* Width of our splash window */
    windowHeight = 320; /* Height of our splash window */

    /* Center the window on screen */
    SetRect(&windowRect, (screenWidth - windowWidth) / 2, (screenHeight - windowHeight) / 2,
            (screenWidth - windowWidth) / 2 + windowWidth,
            (screenHeight - windowHeight) / 2 + windowHeight);

    /* Create the window with standard window manager controls (title bar, close box, draggable) */
    sWindow = NewWindow(NULL, &windowRect, "\pAI for Macintosh", true, documentProc, (WindowPtr)-1,
                        true, 0);

    if (sWindow == NULL) {
        HandleError(kErrWindowCreation, kCtxCreatingMainWindow, false); /* Non-fatal */
        return;
    }

    SetPort(sWindow);

    sInitialized   = true;
    sIsVisible     = false;
    sButtonClicked = false;
}

/* Dispose of the splash window and clean up resources */
void SplashWindow_Dispose(void)
{
    if (!sInitialized) {
        return;
    }

    /* Dispose the window */
    if (sWindow != NULL) {
        DisposeWindow(sWindow);
        sWindow = NULL;
    }

    sInitialized = false;
    sIsVisible   = false;
}

/* Handle update events for the splash window */
void SplashWindow_Update(void)
{
    if (!sInitialized || sWindow == NULL || !sIsVisible)
        return;

    SetPort(sWindow);
    BeginUpdate(sWindow);

    refreshSplashApp(false);

    EndUpdate(sWindow);
}

/* Handle mouse clicks in the content area of the splash window */
Boolean SplashWindow_HandleContentClick(Point localPt)
{
    Boolean wasClicked;

    if (!sInitialized || sWindow == NULL || !sIsVisible)
        return false;

    /* Reset button click state */
    sButtonClicked = false;

    /* The actual event handling is done in main.c through nk_quickdraw_handle_event */
    /* We just check if the button was clicked */
    refreshSplashApp(false);
    
    /* Check if the button was clicked */
    wasClicked = sButtonClicked;
    sButtonClicked = false;

    return wasClicked;
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
        sIsVisible = true;

        /* Force an update */
        refreshSplashApp(false);
    }
    else {
        HideWindow(sWindow);
        sIsVisible = false;
    }
}

/* Determine if the window is visible */
Boolean SplashWindow_IsVisible(void)
{
    if (!sInitialized || sWindow == NULL) {
        return false;
    }

    return sIsVisible;
}