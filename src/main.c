#include <Dialogs.h>
#include <Fonts.h>
#include <Memory.h>
#include <Menus.h>
#include <Quickdraw.h>
#include <Resources.h>
#include <Sound.h>
#include <TextEdit.h>
#include <ToolUtils.h>
#include <Windows.h>

#include "ai_model.h"
#include "constants.h"
#include "error.h"
#include "sound/beepbop.c"
#include "ui/event.h"
#include "ui/menu.h"
#include "ui/window_manager.h"

/* Application mode tracking (used by multiple modules) */
short gAppMode = kModeMainSplash;

/*********************************************************************
 * MAIN PROGRAM
 *********************************************************************/

/* Main entry point - initial function called when application starts
 *
 * This initializes the Macintosh toolbox, sets up UI, and runs the main event loop.
 * Classic Mac applications use a cooperative multitasking model where each
 * application must regularly check for and handle system events.
 */
int main(void)
{
    OSErr err;

    /*----------------------------------------
     * Basic Macintosh Toolbox initialization
     *----------------------------------------*/
    MaxApplZone(); /* Maximize heap space */
    MoreMasters(); /* Allocate additional master pointers for handles */
    MoreMasters(); /* Call twice to ensure plenty of master pointers */

    /* Check available memory early */
    err = CheckMemory();
    if (err != noErr) {
        HandleError(kErrMemoryFull, kCtxLaunchingApp, true); /* Fatal error */
        return 1; /* Should never reach here due to fatal error */
    }

    InitGraf(&qd.thePort); /* QuickDraw */
    InitFonts();           /* Font Manager */
    InitWindows();         /* Window Manager */
    InitMenus();           /* Menu Manager */
    TEInit();              /* TextEdit */
    InitDialogs(NULL);     /* Dialog Manager */
    InitCursor();          /* Set arrow cursor */

    /* Clear any pending events */
    FlushEvents(everyEvent, 0);

    /*----------------------------------------
     * Setup application UI
     *----------------------------------------*/
    /* Set up menu bar */
    Handle menuBar = GetNewMBar(128);
    if (menuBar == NULL) {
        HandleError(kErrResourceNotFound, kCtxLaunchingApp, true); /* Fatal error */
        return 1; /* Should never reach here due to fatal error */
    }

    SetMenuBar(menuBar);

    /* Get Apple menu and append desk accessories */
    MenuHandle appleMenu = GetMenu(kMenuApple);
    if (appleMenu == NULL) {
        HandleError(kErrResourceNotFound, kCtxLaunchingApp, true); /* Fatal error */
        return 1; /* Should never reach here due to fatal error */
    }

    AppendResMenu(appleMenu, 'DRVR');
    DrawMenuBar();

    /* Initialize conversation history */
    InitConversationHistory();

    /* Initialize window manager and default windows */
    WindowManager_Initialize();

    /*----------------------------------------
     * Main event loop
     *----------------------------------------*/
    for (;;) {
        EventRecord event;

        /* Give time to background processes */
        SystemTask();

        /* Perform idle for any audio */
        BeepBopIdle();

        /* Perform idle processing for active window */
        WindowManager_Idle();

        /* Get and process the next event */
        if (GetNextEvent(everyEvent, &event)) {
            /* Delegate all event handling to the window manager */
            WindowManager_HandleEvent(&event);
        }
    }

    return 0;
}