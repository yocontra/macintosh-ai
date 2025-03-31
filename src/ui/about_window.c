#include <Events.h>
#include <Resources.h>
#include <TextEdit.h>
#include <Windows.h>

#include "../constants.h"
#include "../error.h"
#include "about_window.h"

/* Track if the about dialog is currently open */
static Boolean sIsAboutOpen = false;

/* Handle events for the about box (if visible) */
void AboutWindow_HandleEvent(EventRecord *event)
{
    /* Only process events if the about dialog is open */
    if (sIsAboutOpen) {
        if (event->what == mouseDown) {
            /* Any click anywhere in the about box will dismiss it */
            sIsAboutOpen = false;
        }
    }
}

/* Show the About box dialog with app information */
void ShowAboutBox(void)
{
    WindowRef w;
    Handle h;
    OSErr err;

    /* If about box is already open, don't show another one */
    if (sIsAboutOpen) {
        return;
    }

    /* Check memory */
    err = CheckMemory();
    if (err != noErr) {
        HandleError(kErrMemoryFull, kCtxShowingAboutBox, false); /* Non-fatal */
        return;
    }

    /* Create the window */
    w = GetNewWindow(kAboutBoxID, NULL, (WindowPtr)-1);
    if (w == NULL) {
        HandleError(kErrWindowCreation, kCtxShowingAboutBox, false); /* Non-fatal */
        return;
    }

    /* Center the window on screen */
    MoveWindow(w, qd.screenBits.bounds.right / 2 - w->portRect.right / 2,
               qd.screenBits.bounds.bottom / 2 - w->portRect.bottom / 2, false);
    ShowWindow(w);
    SetPort(w);

    /* Load and display the text resource */
    h = GetResource('TEXT', kAboutBoxID);
    if (h == NULL) {
        HandleError(kErrResourceNotFound, kCtxAboutBoxText, false); /* Non-fatal */
        DisposeWindow(w);
        return;
    }

    HLock(h);
    Rect r = w->portRect;
    InsetRect(&r, 10, 10);
    TETextBox(*h, GetHandleSize(h), &r, teJustLeft);
    ReleaseResource(h);

    /* Mark that the about box is open */
    sIsAboutOpen = true;
}