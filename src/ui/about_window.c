#include <Events.h>
#include <Resources.h>
#include <TextEdit.h>
#include <Windows.h>

#include "../constants.h"
#include "../error.h"
#include "about_window.h"

/* Private module variables */
static WindowRef sWindow    = NULL;
static Boolean sInitialized = false;
static Boolean sIsVisible   = false;

/* Initialize and create the about window */
void AboutWindow_Initialize(void)
{
    /* Prevent double initialization */
    if (sInitialized) {
        return;
    }

    /* We don't actually create anything yet - about window is created on demand */
    sInitialized = true;
}

/* Dispose of the about window and clean up resources */
void AboutWindow_Dispose(void)
{
    /* The about window is automatically disposed when closed */
    if (sWindow != NULL) {
        /* Make sure window is closed */
        DisposeWindow(sWindow);
        sWindow = NULL;
    }
    sInitialized = false;
    sIsVisible   = false;
}

/* Handle events for the about box (if visible) */
void AboutWindow_HandleEvent(EventRecord *event)
{
    /* Only process events if the about dialog is open */
    if (sIsVisible) {
        if (event->what == mouseDown) {
            /* Any click anywhere in the about box will dismiss it */
            AboutWindow_Show(false);
        }
    }
}

/* Handle update events for the about window */
void AboutWindow_Update(void)
{
    if (!sIsVisible || sWindow == NULL) {
        return;
    }

    /* About window is mostly static, just validate the update region */
    SetPort(sWindow);
    BeginUpdate(sWindow);
    EndUpdate(sWindow);
}

/* Get the window reference for the about window */
WindowRef AboutWindow_GetWindowRef(void)
{
    return sWindow;
}

/* Show or hide the about window */
void AboutWindow_Show(Boolean visible)
{
    Handle h;
    OSErr err;

    if (visible && !sIsVisible) {
        /* Check memory */
        err = CheckMemory();
        if (err != noErr) {
            HandleError(kErrMemoryFull, kCtxShowingAboutBox, false); /* Non-fatal */
            return;
        }

        /* Create the window */
        sWindow = GetNewWindow(kAboutBoxID, NULL, (WindowPtr)-1);
        if (sWindow == NULL) {
            HandleError(kErrWindowCreation, kCtxShowingAboutBox, false); /* Non-fatal */
            return;
        }

        /* Center the window on screen */
        MoveWindow(sWindow, qd.screenBits.bounds.right / 2 - sWindow->portRect.right / 2,
                   qd.screenBits.bounds.bottom / 2 - sWindow->portRect.bottom / 2, false);
        ShowWindow(sWindow);
        SelectWindow(sWindow);
        SetPort(sWindow);

        /* Load and display the text resource */
        h = GetResource('TEXT', kAboutBoxID);
        if (h == NULL) {
            HandleError(kErrResourceNotFound, kCtxAboutBoxText, false); /* Non-fatal */
            DisposeWindow(sWindow);
            sWindow = NULL;
            return;
        }

        HLock(h);
        Rect r = sWindow->portRect;
        InsetRect(&r, 10, 10);
        TETextBox(*h, GetHandleSize(h), &r, teJustLeft);
        ReleaseResource(h);

        /* Mark window as visible */
        sIsVisible = true;
    }
    else if (!visible && sIsVisible) {
        /* Close the window */
        if (sWindow != NULL) {
            DisposeWindow(sWindow);
            sWindow = NULL;
        }
        sIsVisible = false;
    }
}

/* Determine if the window is visible */
Boolean AboutWindow_IsVisible(void)
{
    return sIsVisible;
}

/* Legacy function for backwards compatibility */
void ShowAboutBox(void)
{
    AboutWindow_Initialize();
    AboutWindow_Show(true);
}