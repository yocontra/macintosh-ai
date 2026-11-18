#include <Dialogs.h>
#include <Events.h>
#include <Fonts.h>
#include <Memory.h>
#include <Menus.h>
#include <Processes.h>
#include <Quickdraw.h>
#include <Resources.h>
#include <Sound.h>
#include <TextEdit.h>
#include <Windows.h>

#include "chatbot/api_client.h"
#include "constants.h"
#include "error.h"
#include "prefs.h"
#include "sound/beepbop.h"
#include "ui/chat_window.h"
#include "ui/event.h"
#include "ui/menu.h"
#include "ui/window_manager.h"

/* Application mode tracking (used by multiple modules) */
short gAppMode = kModeMainSplash;

/* Tick count until which main keeps asking to be the front process. */
static unsigned long sFrontProcessUntil = 0;

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

    InitGraf(&qd.thePort); /* QuickDraw */
    InitFonts();           /* Font Manager */
    InitWindows();         /* Window Manager */
    InitMenus();           /* Menu Manager */
    TEInit();              /* TextEdit */
    InitDialogs(NULL);     /* Dialog Manager */
    InitCursor();          /* Set arrow cursor */

    /* Memory checks need the Dialog Manager for their error alert */
    err = CheckMemory();
    if (err != noErr) {
        HandleError(kErrMemoryFull, kCtxLaunchingApp, true); /* Fatal error */
        return 1; /* Should never reach here due to fatal error */
    }

    /* Clear pending input events without discarding update or disk events */
    FlushEvents(mDownMask | mUpMask | keyDownMask | keyUpMask | autoKeyMask, 0);

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

    /* Load persisted settings, then bring up the network client. API_Init only
       initializes the transport and TLS context; it does not connect. */
    Prefs_Load();
    API_Init();

    /* Initialize window manager and default windows */
    if (!WindowManager_Initialize()) {
        HandleError(kErrWindowCreation, kCtxCreatingMainWindow, true); /* Fatal */
        return 1; /* Should never reach here due to fatal error */
    }

    /* An application started from Startup Items is not automatically the
       active process: the Finder keeps the menu bar until something brings us
       forward, and until then the first click on our window is only consumed
       by the activation and every keyboard shortcut goes to the Finder.
       Ask once here, and again from the idle loop for the first few seconds:
       at this point the Finder is often still finishing its own startup and
       the first request is ignored. */
    {
        ProcessSerialNumber psn;

        if (GetCurrentProcess(&psn) == noErr) {
            SetFrontProcess(&psn);
        }
    }

    /*----------------------------------------
     * Main event loop
     *----------------------------------------*/
    sFrontProcessUntil = TickCount() + 300; /* ~5 s at 60 Hz */

    for (;;) {
        EventRecord event;

        /* Give time to background processes */
        SystemTask();

        /* Perform idle for any audio */
        BeepBopIdle();

        /* Drive the streaming API request and blink the insertion point */
        ChatWindow_Idle();

        /* Keep asking to be the active process for the first few seconds: an
           application launched from Startup Items is not front automatically,
           and while the Finder is still starting it ignores the first
           request. After that this stops for good. */
        if (TickCount() < sFrontProcessUntil) {
            ProcessSerialNumber psn;

            if (GetCurrentProcess(&psn) == noErr) {
                SetFrontProcess(&psn);
            }
        }

        /* WaitNextEvent yields time to other applications while waiting */
        if (WaitNextEvent(everyEvent, &event, 1, NULL)) {
            /* Dispatch command-key menu equivalents */
            if (event.what == keyDown) {
                long menuCommand = MenuKey(event.message & charCodeMask);

                if (menuCommand != 0) {
                    HiliteMenu(0);
                    DoMenuCommand(menuCommand);
                    continue;
                }
            }

            /* Delegate all other events to the window manager */
            WindowManager_HandleEvent(&event);
        }
    }

    /* Not reached: QuitApplication() ends the process */
    API_Shutdown();

    return 0;
}
