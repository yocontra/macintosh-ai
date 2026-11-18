#include <Dialogs.h>
#include <TextUtils.h>
#include <Windows.h>

#include "../constants.h"
#include "../error.h"
#include "about_window.h"

/* Version string is supplied by CMake; plain source builds report "dev". */
#ifndef APP_VERSION_STR
#define APP_VERSION_STR "dev"
#endif

/* Item numbers in DITL 128 (see main.r) */
enum { kAboutOKItem = 1 };

/* Show the About box as a modal dialog */
void AboutWindow_Show(void)
{
    DialogPtr dialog;
    GrafPtr savePort;
    Str255 version;
    short item;

    GetPort(&savePort);

    dialog = GetNewDialog(kAboutBoxID, NULL, (WindowPtr)-1);
    if (dialog == NULL) {
        HandleError(kErrResourceNotFound, kCtxShowingAboutBox, false);
        SetPort(savePort);
        return;
    }

    SetPort(dialog);

    CopyCStringToPascal(APP_VERSION_STR, version);
    ParamText(version, "\p", "\p", "\p");

    ShowWindow(dialog);
    SetDialogDefaultItem(dialog, kAboutOKItem);

    for (;;) {
        ModalDialog(NULL, &item);
        if (item == kAboutOKItem)
            break;
    }

    DisposeDialog(dialog);
    SetPort(savePort);
}
