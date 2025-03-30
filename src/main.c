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
#include "ui/chat_window.h"
#include "ui/event.h"
#include "ui/menu.h"
#include "ui/splash_window.h"
#include "ui/window_manager.h"

/* Application mode tracking */
short gAppMode = kModeMainSplash;

/* Handle key down events */
static void HandleKeyDown(EventRecord *event)
{
    char key = event->message & charCodeMask;

    /* Handle Command key shortcuts for both modes */
    if (event->modifiers & cmdKey) {
        /* Command key shortcuts */
        if (key == 'l' || key == 'L') {
            /* Cmd-L: Open chat window (from any mode) */
            ChatWindow_Show(true);
            SplashWindow_Show(false);
            gAppMode = kModeChatWindow;
        }
        else {
            /* Other command key combinations */
            UpdateMenus();
            DoMenuCommand(MenuKey(key));
        }
        return;
    }

    /* Handle mode-specific keyboard interactions */
    switch (gAppMode) {
    case kModeMainSplash:
        /* The only action in splash mode is Cmd-L which was handled above */
        break;

    case kModeChatWindow:
        /* Only handle chat input if the chat window is active */
        if (ChatWindow_IsVisible() && FrontWindow() == ChatWindow_GetWindowRef()) {
            /* Process key presses in the chat window */
            ChatWindow_HandleKeyDown(key, (event->modifiers & shiftKey) != 0, false);
        }
        break;
    }
}

/* Handle mouse down events */
static void HandleMouseDown(EventRecord *event)
{
    WindowRef window;
    short part = FindWindow(event->where, &window);

    /* Process mouse click based on which part of which window was clicked */
    switch (part) {
    case inGoAway:
        /* User clicked window close box */
        if (TrackGoAway(window, event->where)) {
            if (window == SplashWindow_GetWindowRef()) {
                /* Closing main window quits app */
                QuitApplication(false);
            }
            else if (window == ChatWindow_GetWindowRef()) {
                /* Closing chat window returns to splash screen */
                ChatWindow_Show(false);
                SplashWindow_Show(true);
                gAppMode = kModeMainSplash;
            }
            else {
                DisposeWindow(window);
            }
        }
        break;

    case inDrag:
        /* User is dragging a window */
        DragWindow(window, event->where, &qd.screenBits.bounds);
        break;

    case inMenuBar:
        /* User clicked in menu bar */
        UpdateMenus();
        DoMenuCommand(MenuSelect(event->where));
        break;

    case inContent:
        /* User clicked in window content */
        if (window != FrontWindow()) {
            /* Activate clicked window */
            SelectWindow(window);
        }
        else if (window == SplashWindow_GetWindowRef()) {
            /* Handle click inside splash window */
            Point mousePt = event->where;
            GlobalToLocal(&mousePt);

            /* Check if start chat button was clicked */
            if (SplashWindow_HandleContentClick(mousePt)) {
                /* Open chat window if button was clicked */
                ChatWindow_Show(true);
                SplashWindow_Show(false);
                gAppMode = kModeChatWindow;
            }
        }
        else if (window == ChatWindow_GetWindowRef()) {
            /* Handle click inside chat window */
            Point mousePt = event->where;
            GlobalToLocal(&mousePt);
            ChatWindow_HandleContentClick(mousePt);
        }
        break;

    case inSysWindow:
        /* Handle clicks in system windows (desk accessories) */
        SystemClick(event, window);
        break;
    }
}

/* Handle updates for any window */
static void HandleWindowUpdate(WindowRef window)
{
    /* Route to the appropriate window module based on window reference */
    if (window == SplashWindow_GetWindowRef()) {
        SplashWindow_Update();
    }
    else if (window == ChatWindow_GetWindowRef()) {
        ChatWindow_Update();
    }
}

/* Handle window activation/deactivation */
static void HandleActivate(EventRecord *event)
{
    WindowRef window       = (WindowRef)event->message;
    Boolean becomingActive = (event->modifiers & activeFlag) != 0;

    if (window == ChatWindow_GetWindowRef()) {
        ChatWindow_HandleActivate(becomingActive);
    }
}

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

    /* Initialize our windows */
    SplashWindow_Initialize();
    ChatWindow_Initialize();

    /* Show the splash window initially */
    SplashWindow_Show(true);
    gAppMode = kModeMainSplash;

    /*----------------------------------------
     * Main event loop
     *----------------------------------------*/
    for (;;) {
        EventRecord event;

        /* Give time to background processes */
        SystemTask();

        /* Make the text cursor blink */
        if (gAppMode == kModeChatWindow) {
            ChatWindow_Idle();
        }

        /* Get and process the next event */
        if (GetNextEvent(everyEvent, &event)) {
            switch (event.what) {
            case keyDown:
                HandleKeyDown(&event);
                break;

            case mouseDown:
                HandleMouseDown(&event);
                break;

            case activateEvt:
                HandleActivate(&event);
                break;

            case updateEvt:
                HandleWindowUpdate((WindowRef)event.message);
                break;
            }
        }
    }

    return 0;
}