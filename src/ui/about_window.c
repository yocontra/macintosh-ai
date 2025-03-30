#include <Resources.h>
#include <TextEdit.h>
#include <Windows.h>

#include "../constants.h"
#include "../error.h"
#include "about_window.h"

/* Show the About box dialog with app information */
void ShowAboutBox(void)
{
    WindowRef w;
    Handle h;
    OSErr err;

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

    /* Wait for user click to dismiss */
    while (!Button())
        ;
    while (Button())
        ;
    FlushEvents(everyEvent, 0);
    DisposeWindow(w);
}