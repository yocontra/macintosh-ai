#include <Dialogs.h>
#include <Memory.h>
#include <Sound.h>
#include <stdio.h>
#include <string.h>
#include <TextUtils.h>
#include <Windows.h>

#include "constants.h"
#include "error.h"

/*********************************************************************
 * ERROR HANDLING FUNCTIONS
 *********************************************************************/

/* Build the context line for ALRT/DITL 200 (item 1), including the error code */
static void BuildContextText(Str255 contextText, short errorCode, short contextID)
{
    const char *text;
    char line[96];
    short len;

    switch (contextID) {
    case kCtxLaunchingApp:
        text = "Error launching app";
        break;
    case kCtxCreatingMainWindow:
        text = "Error creating main window";
        break;
    case kCtxCreatingButton:
        text = "Error creating button";
        break;
    case kCtxShowingAboutBox:
        text = "Error showing about box";
        break;
    case kCtxAboutBoxText:
        text = "Error loading about box text";
        break;
    case kCtxOpeningChatWindow:
        text = "Error opening chat window";
        break;
    case kCtxCreatingResponseArea:
        text = "Error creating response area";
        break;
    case kCtxCreatingPromptArea:
        text = "Error creating prompt area";
        break;
    case kCtxGeneratingResponse:
        text = "Error generating response";
        break;
    case kCtxQuittingApplication:
        text = "Quitting application";
        break;
    default:
        text = "Unknown error occurred";
        break;
    }

    len = (short)sprintf(line, "%s (error %d)", text, (int)errorCode);
    if (len < 0)
        len = 0;
    if (len > 255)
        len = 255;

    contextText[0] = (unsigned char)len;
    memcpy(contextText + 1, line, (size_t)len);
}

/* Error handling function with alert dialog and optional fatal exit.
   DITL 200 item 1 is "^0" (context) and item 2 is "^1" (fatal sentence). */
void HandleError(short errorCode, short contextID, Boolean fatal)
{
    Str255 contextText;

    /* Double beep to alert the user of an error */
    SysBeep(10);
    SysBeep(20);

    /* Set both substitution strings once; the fatal branch used to overwrite
       the context text with the generic sentence, blanking DITL item 1. */
    BuildContextText(contextText, errorCode, contextID);

    if (fatal) {
        ParamText(contextText,
                  "\pThis is a fatal error. The application will exit when you click OK.", "\p",
                  "\p");
        StopAlert(200, NULL);

        /* For fatal errors, it's safer to use ExitToShell directly
           rather than trying to clean up resources that might be corrupt */
        ExitToShell();
    }
    else {
        ParamText(contextText, "\p", "\p", "\p");
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