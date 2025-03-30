#include <Dialogs.h>
#include <Memory.h>
#include <Sound.h>
#include <TextUtils.h>
#include <Windows.h>

#include "constants.h"
#include "error.h"

/*********************************************************************
 * ERROR HANDLING FUNCTIONS
 *********************************************************************/

/* Error handling function with alert dialog and optional fatal exit */
void HandleError(short errorCode, short contextID, Boolean fatal)
{
    /* Double beep to alert the user of an error */
    SysBeep(10);
    SysBeep(20);

    /* Display simple note alert with error info */
    switch (contextID) {
    case kCtxLaunchingApp:
        ParamText("\pError launching app", "\p", "\p", "\p");
        break;
    case kCtxCreatingMainWindow:
        ParamText("\pError creating main window", "\p", "\p", "\p");
        break;
    case kCtxCreatingButton:
        ParamText("\pError creating button", "\p", "\p", "\p");
        break;
    case kCtxShowingAboutBox:
        ParamText("\pError showing about box", "\p", "\p", "\p");
        break;
    case kCtxAboutBoxText:
        ParamText("\pError loading about box text", "\p", "\p", "\p");
        break;
    case kCtxOpeningChatWindow:
        ParamText("\pError opening chat window", "\p", "\p", "\p");
        break;
    case kCtxCreatingResponseArea:
        ParamText("\pError creating response area", "\p", "\p", "\p");
        break;
    case kCtxCreatingPromptArea:
        ParamText("\pError creating prompt area", "\p", "\p", "\p");
        break;
    case kCtxGeneratingResponse:
        ParamText("\pError generating response", "\p", "\p", "\p");
        break;
    case kCtxQuittingApplication:
        ParamText("\pQuitting application", "\p", "\p", "\p");
        break;
    default:
        ParamText("\pUnknown error occurred", "\p", "\p", "\p");
        break;
    }

    /* Display the alert */
    if (fatal) {
        /* For fatal errors, show a different kind of alert */
        ParamText("\pThis is a fatal error. The application will exit when you click OK.", "\p",
                  "\p", "\p");
        StopAlert(200, NULL);

        /* For fatal errors, it's safer to use ExitToShell directly
           rather than trying to clean up resources that might be corrupt */
        ExitToShell();
    }
    else {
        /* For non-fatal errors, use standard note alert */
        NoteAlert(200, NULL);
    }
}

/* Check available memory */
OSErr CheckMemory(void)
{
    long totalMem, contigMem;

    /* Check free memory */
    PurgeSpace(&totalMem, &contigMem);

    if (contigMem < 50 * 1024) { /* Require at least 50K of contiguous memory */
        return memFullErr;
    }

    if (totalMem < 100 * 1024) { /* Require at least 100K of total memory */
        return memFullErr;
    }

    return noErr;
}

/* QuitApplication is now defined in event.c */