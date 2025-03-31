#include <Devices.h>
#include <Events.h>
#include <Menus.h>
#include <Windows.h>

#include "../constants.h"
#include "../error.h"
#include "about_window.h"
#include "chat_window.h"
#include "event.h"
#include "menu.h"
#include "splash_window.h"
#include "window_manager.h"

/*
 * Window Manager module that coordinates operations between
 * individual window modules using a standardized interface
 */

/* Window modules registration */
static WindowModule gWindowModules[3];

/* Current active window tracking */
static WindowType gForegroundWindowType = kWindowTypeSplash;

/* Register window module functions */
static void RegisterWindowModules(void)
{
    /* Splash window */
    gWindowModules[kWindowTypeSplash].initialize   = SplashWindow_Initialize;
    gWindowModules[kWindowTypeSplash].dispose      = SplashWindow_Dispose;
    gWindowModules[kWindowTypeSplash].show         = SplashWindow_Show;
    gWindowModules[kWindowTypeSplash].handleEvent  = SplashWindow_HandleEvent;
    gWindowModules[kWindowTypeSplash].getWindowRef = SplashWindow_GetWindowRef;
    gWindowModules[kWindowTypeSplash].isVisible    = SplashWindow_IsVisible;
    gWindowModules[kWindowTypeSplash].initialized  = false;
    gWindowModules[kWindowTypeSplash].visible      = false;

    /* Chat window */
    gWindowModules[kWindowTypeChat].initialize   = ChatWindow_Initialize;
    gWindowModules[kWindowTypeChat].dispose      = ChatWindow_Dispose;
    gWindowModules[kWindowTypeChat].show         = ChatWindow_Show;
    gWindowModules[kWindowTypeChat].handleEvent  = ChatWindow_HandleEvent;
    gWindowModules[kWindowTypeChat].getWindowRef = ChatWindow_GetWindowRef;
    gWindowModules[kWindowTypeChat].isVisible    = ChatWindow_IsVisible;
    gWindowModules[kWindowTypeChat].initialized  = false;
    gWindowModules[kWindowTypeChat].visible      = false;

    /* About window */
    gWindowModules[kWindowTypeAbout].initialize   = AboutWindow_Initialize;
    gWindowModules[kWindowTypeAbout].dispose      = AboutWindow_Dispose;
    gWindowModules[kWindowTypeAbout].show         = AboutWindow_Show;
    gWindowModules[kWindowTypeAbout].handleEvent  = AboutWindow_HandleEvent;
    gWindowModules[kWindowTypeAbout].getWindowRef = AboutWindow_GetWindowRef;
    gWindowModules[kWindowTypeAbout].isVisible    = AboutWindow_IsVisible;
    gWindowModules[kWindowTypeAbout].initialized  = false;
    gWindowModules[kWindowTypeAbout].visible      = false;
}

/* Initialize the window manager and the initial window */
void WindowManager_Initialize(void)
{
    /* Register all window modules first */
    RegisterWindowModules();

    /* Initialize splash window as the default view */
    gWindowModules[kWindowTypeSplash].initialize();
    gWindowModules[kWindowTypeSplash].initialized = true;
    gWindowModules[kWindowTypeSplash].show(true);
    gWindowModules[kWindowTypeSplash].visible = true;

    /* Set the splash window as foreground */
    gForegroundWindowType = kWindowTypeSplash;
}

/* Dispose all windows and clean up resources */
void WindowManager_Dispose(void)
{
    int i;

    /* Dispose windows that were initialized */
    for (i = 0; i < 3; i++) {
        if (gWindowModules[i].initialized) {
            gWindowModules[i].dispose();
            gWindowModules[i].initialized = false;
            gWindowModules[i].visible     = false;
        }
    }
}

/* Open a specific type of window */
void WindowManager_OpenWindow(WindowType windowType)
{
    /* Initialize the window if it hasn't been initialized yet */
    if (!gWindowModules[windowType].initialized) {
        gWindowModules[windowType].initialize();
        gWindowModules[windowType].initialized = true;
    }

    /* Show the window */
    gWindowModules[windowType].show(true);
    gWindowModules[windowType].visible = true;

    /* Handle special case for About window (doesn't change foreground) */
    if (windowType != kWindowTypeAbout) {
        WindowManager_SetForegroundWindow(windowType);
    }
}

/* Close a specific type of window */
void WindowManager_CloseWindow(WindowType windowType)
{
    /* Only hide the window if it was initialized */
    if (gWindowModules[windowType].initialized) {
        gWindowModules[windowType].show(false);
        gWindowModules[windowType].visible = false;
    }
}

/* Set the foreground window type and update application state */
void WindowManager_SetForegroundWindow(WindowType windowType)
{
    WindowRef windowRef;

    /* Ignore if the window hasn't been initialized */
    if (!gWindowModules[windowType].initialized) {
        return;
    }

    /* Get the window reference */
    windowRef = gWindowModules[windowType].getWindowRef();
    if (windowRef == NULL) {
        return;
    }

    /* Bring the window to front and select it */
    ShowWindow(windowRef);
    SelectWindow(windowRef);

    /* Update the foreground window type */
    gForegroundWindowType = windowType;

    /* Update application mode */
    switch (windowType) {
    case kWindowTypeSplash:
        gAppMode = kModeMainSplash;
        break;

    case kWindowTypeChat:
        gAppMode = kModeChatWindow;
        break;

    default:
        /* No change to app mode for other window types */
        break;
    }

    /* Update menus based on active window */
    UpdateMenus();
}

/* Get the current foreground window type */
WindowType WindowManager_GetForegroundWindowType(void)
{
    return gForegroundWindowType;
}

/* Get window reference for a specific window type */
WindowRef WindowManager_GetWindowRef(WindowType windowType)
{
    if (windowType >= 0 && windowType < 3 && gWindowModules[windowType].initialized) {
        return gWindowModules[windowType].getWindowRef();
    }
    return NULL;
}

/* Check if a window is visible */
Boolean WindowManager_IsWindowVisible(WindowType windowType)
{
    if (windowType >= 0 && windowType < 3 && gWindowModules[windowType].initialized) {
        return gWindowModules[windowType].isVisible();
    }
    return false;
}

/* Handle standard window operations like dragging, closing, etc. */
static Boolean HandleStandardWindowOps(EventRecord *event)
{
    WindowRef window;
    short part;

    if (event->what == mouseDown) {
        part = FindWindow(event->where, &window);

        if (part == inMenuBar) {
            /* Handle menu clicks */
            UpdateMenus();
            DoMenuCommand(MenuSelect(event->where));
            return true;
        }
        else if (part == inSysWindow) {
            /* Handle clicks in system windows (desk accessories) */
            SystemClick(event, window);
            return true;
        }
        else if (part == inDrag) {
            /* User is dragging a window */
            DragWindow(window, event->where, &qd.screenBits.bounds);
            return true;
        }
        else if (part == inGoAway) {
            /* User clicked window close box */
            if (TrackGoAway(window, event->where)) {
                if (window == gWindowModules[kWindowTypeSplash].getWindowRef()) {
                    /* Closing main window quits app */
                    QuitApplication(false);
                }
                else if (window == gWindowModules[kWindowTypeChat].getWindowRef()) {
                    /* Closing chat window returns to splash screen */
                    WindowManager_CloseWindow(kWindowTypeChat);
                    WindowManager_OpenWindow(kWindowTypeSplash);
                }
                else if (window == gWindowModules[kWindowTypeAbout].getWindowRef()) {
                    /* Just close the about window */
                    WindowManager_CloseWindow(kWindowTypeAbout);
                }
                else {
                    DisposeWindow(window);
                }
            }
            return true;
        }
        else if (part == inContent && window != FrontWindow()) {
            /* Activate clicked window */
            SelectWindow(window);

            /* Update foreground window type */
            if (window == gWindowModules[kWindowTypeSplash].getWindowRef()) {
                WindowManager_SetForegroundWindow(kWindowTypeSplash);
            }
            else if (window == gWindowModules[kWindowTypeChat].getWindowRef()) {
                WindowManager_SetForegroundWindow(kWindowTypeChat);
            }
            return true;
        }
    }

    return false;
}

/* Handle global key commands */
static Boolean HandleGlobalKeys(EventRecord *event)
{
    char key;

    if (event->what == keyDown && (event->modifiers & cmdKey)) {
        key = event->message & charCodeMask;

        /* Cmd-L to open chat window */
        if (key == 'l' || key == 'L') {
            WindowManager_OpenWindow(kWindowTypeChat);
            WindowManager_CloseWindow(kWindowTypeSplash);
            return true;
        }

        /* Cmd-W to close window */
        else if (key == 'w' || key == 'W') {
            WindowRef window = FrontWindow();
            if (window) {
                if (GetWindowKind(window) < 0) {
                    /* Close desk accessory */
                    CloseDeskAcc(GetWindowKind(window));
                }
                else if (window == gWindowModules[kWindowTypeChat].getWindowRef()) {
                    /* Close chat window and switch back to splash mode */
                    WindowManager_CloseWindow(kWindowTypeChat);
                    WindowManager_OpenWindow(kWindowTypeSplash);
                }
                else if (window == gWindowModules[kWindowTypeSplash].getWindowRef()) {
                    /* Closing main window quits app */
                    QuitApplication(false);
                }
                else {
                    /* Other window */
                    DisposeWindow(window);
                }
            }
            return true;
        }

        /* Cmd-Q to quit application */
        else if (key == 'q' || key == 'Q') {
            QuitApplication(false);
            return true;
        }
    }

    return false;
}

/* Handle window activation events */
static void HandleWindowActivation(EventRecord *event)
{
    WindowRef window;
    Boolean becomingActive;

    if (event->what == activateEvt) {
        window         = (WindowRef)event->message;
        becomingActive = (event->modifiers & activeFlag) != 0;

        /* Update the foreground window type if a window is being activated */
        if (becomingActive) {
            if (window == gWindowModules[kWindowTypeSplash].getWindowRef()) {
                gForegroundWindowType = kWindowTypeSplash;
                gAppMode              = kModeMainSplash;
            }
            else if (window == gWindowModules[kWindowTypeChat].getWindowRef()) {
                gForegroundWindowType = kWindowTypeChat;
                gAppMode              = kModeChatWindow;
            }
        }
    }
}

/* Route update events to the appropriate window */
static Boolean RouteUpdateEvent(EventRecord *event)
{
    WindowRef window;
    int i;

    if (event->what == updateEvt) {
        window = (WindowRef)event->message;

        /* Find which window needs updating */
        for (i = 0; i < 3; i++) {
            if (window == gWindowModules[i].getWindowRef() && gWindowModules[i].isVisible()) {
                gWindowModules[i].handleEvent(event);
                return true;
            }
        }
    }

    return false;
}

/* Handle all event types and route to appropriate handler */
void WindowManager_HandleEvent(EventRecord *event)
{
    /* Handle standard window operations first */
    if (HandleStandardWindowOps(event)) {
        return;
    }

    /* Handle global key commands */
    if (HandleGlobalKeys(event)) {
        return;
    }

    /* Handle window activation events */
    HandleWindowActivation(event);

    /* Route update events to specific windows */
    if (RouteUpdateEvent(event)) {
        return;
    }

    /* Route other events to the foreground window */
    if (gForegroundWindowType >= 0 && gForegroundWindowType < 3 &&
        gWindowModules[gForegroundWindowType].initialized &&
        gWindowModules[gForegroundWindowType].visible) {
        gWindowModules[gForegroundWindowType].handleEvent(event);
    }
}

/* Perform idle-time processing */
void WindowManager_Idle(void)
{
    /* Only need idle processing for chat window */
    if (gForegroundWindowType == kWindowTypeChat && gWindowModules[kWindowTypeChat].initialized &&
        gWindowModules[kWindowTypeChat].visible) {
        ChatWindow_Idle(); /* Direct call since not part of standard interface */
    }
}