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
 * individual window modules. Centralizes window state management and event routing.
 */

/* Current active window tracking */
static WindowType gForegroundWindowType = kWindowTypeSplash;

/* Window initialization state tracking */
static Boolean gWindowInitialized[3] = {false, false, false};

/* Initialize the window manager and the initial window */
void WindowManager_Initialize(void)
{
    /* Initialize splash window first as it's our default view */
    SplashWindow_Initialize();
    gWindowInitialized[kWindowTypeSplash] = true;
    SplashWindow_Show(true);

    /* Set the splash window as foreground */
    gForegroundWindowType = kWindowTypeSplash;
}

/* Dispose all windows and clean up resources */
void WindowManager_Dispose(void)
{
    /* Dispose windows that were initialized */
    if (gWindowInitialized[kWindowTypeSplash]) {
        SplashWindow_Dispose();
        gWindowInitialized[kWindowTypeSplash] = false;
    }

    if (gWindowInitialized[kWindowTypeChat]) {
        ChatWindow_Dispose();
        gWindowInitialized[kWindowTypeChat] = false;
    }

    if (gWindowInitialized[kWindowTypeAbout]) {
        AboutWindow_Dispose();
        gWindowInitialized[kWindowTypeAbout] = false;
    }
}

/* Open a specific type of window */
void WindowManager_OpenWindow(WindowType windowType)
{
    /* Initialize the window if it hasn't been initialized yet */
    if (!gWindowInitialized[windowType]) {
        switch (windowType) {
        case kWindowTypeSplash:
            SplashWindow_Initialize();
            break;

        case kWindowTypeChat:
            ChatWindow_Initialize();
            break;

        case kWindowTypeAbout:
            AboutWindow_Initialize();
            break;
        }
        gWindowInitialized[windowType] = true;
    }

    /* Show the window based on type */
    switch (windowType) {
    case kWindowTypeSplash:
        SplashWindow_Show(true);
        WindowManager_SetForegroundWindow(kWindowTypeSplash);
        break;

    case kWindowTypeChat:
        ChatWindow_Show(true);
        WindowManager_SetForegroundWindow(kWindowTypeChat);
        break;

    case kWindowTypeAbout:
        AboutWindow_Show(true);
        /* About box doesn't change the foreground window type */
        break;
    }
}

/* Close a specific type of window */
void WindowManager_CloseWindow(WindowType windowType)
{
    /* Only hide the window if it was initialized */
    if (gWindowInitialized[windowType]) {
        switch (windowType) {
        case kWindowTypeSplash:
            SplashWindow_Show(false);
            break;

        case kWindowTypeChat:
            ChatWindow_Show(false);
            break;

        case kWindowTypeAbout:
            AboutWindow_Show(false);
            break;
        }
    }
}

/* Set the foreground window type and update application state */
void WindowManager_SetForegroundWindow(WindowType windowType)
{
    WindowRef windowRef;

    /* Ignore if the window hasn't been initialized */
    if (!gWindowInitialized[windowType]) {
        return;
    }

    /* Get the window reference */
    windowRef = WindowManager_GetWindowRef(windowType);
    if (windowRef == NULL) {
        return;
    }

    /* Bring the window to front and select it */
    ShowWindow(windowRef);
    SelectWindow(windowRef);

    /* Update the foreground window type */
    gForegroundWindowType = windowType;

    /* Update application mode for compatibility with existing code */
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
    switch (windowType) {
    case kWindowTypeSplash:
        return SplashWindow_GetWindowRef();

    case kWindowTypeChat:
        return ChatWindow_GetWindowRef();

    case kWindowTypeAbout:
        return AboutWindow_GetWindowRef();

    default:
        return NULL;
    }
}

/* Check if a window is visible */
Boolean WindowManager_IsWindowVisible(WindowType windowType)
{
    switch (windowType) {
    case kWindowTypeSplash:
        return SplashWindow_IsVisible();

    case kWindowTypeChat:
        return ChatWindow_IsVisible();

    case kWindowTypeAbout:
        return AboutWindow_IsVisible();

    default:
        return false;
    }
}

/* Handle all event types and route to appropriate handler */
void WindowManager_HandleEvent(EventRecord *event)
{
    WindowRef window;

    /* Handle menu bar clicks (applies to all windows) */
    if (event->what == mouseDown) {
        short part = FindWindow(event->where, &window);
        if (part == inMenuBar) {
            UpdateMenus();
            DoMenuCommand(MenuSelect(event->where));
            return;
        }
        else if (part == inSysWindow) {
            /* Handle clicks in system windows (desk accessories) */
            SystemClick(event, window);
            return;
        }
        else if (part == inDrag) {
            /* User is dragging a window */
            DragWindow(window, event->where, &qd.screenBits.bounds);
            return;
        }
        else if (part == inGoAway) {
            /* User clicked window close box */
            if (TrackGoAway(window, event->where)) {
                if (window == SplashWindow_GetWindowRef()) {
                    /* Closing main window quits app */
                    QuitApplication(false);
                }
                else if (window == ChatWindow_GetWindowRef()) {
                    /* Closing chat window returns to splash screen */
                    WindowManager_CloseWindow(kWindowTypeChat);
                    WindowManager_OpenWindow(kWindowTypeSplash);
                }
                else {
                    DisposeWindow(window);
                }
            }
            return;
        }
        else if (part == inContent && window != FrontWindow()) {
            /* Activate clicked window */
            SelectWindow(window);

            /* Update foreground window type */
            if (window == SplashWindow_GetWindowRef()) {
                WindowManager_SetForegroundWindow(kWindowTypeSplash);
            }
            else if (window == ChatWindow_GetWindowRef()) {
                WindowManager_SetForegroundWindow(kWindowTypeChat);
            }
            return;
        }
    }

    /* Handle window activation */
    if (event->what == activateEvt) {
        window                 = (WindowRef)event->message;
        Boolean becomingActive = (event->modifiers & activeFlag) != 0;

        /* Update the foreground window type if a window is being activated */
        if (becomingActive) {
            if (window == SplashWindow_GetWindowRef()) {
                gForegroundWindowType = kWindowTypeSplash;
                gAppMode              = kModeMainSplash;
            }
            else if (window == ChatWindow_GetWindowRef()) {
                gForegroundWindowType = kWindowTypeChat;
                gAppMode              = kModeChatWindow;
            }
        }
    }

    /* For update events, we no longer need to call Update directly.
     * Each window's HandleEvent function will handle update events for its own window. */

    /* Global key command: Cmd-L to open chat window */
    if (event->what == keyDown) {
        char key = event->message & charCodeMask;
        if ((event->modifiers & cmdKey) && (key == 'l' || key == 'L')) {
            WindowManager_OpenWindow(kWindowTypeChat);
            WindowManager_CloseWindow(kWindowTypeSplash);
            return;
        }
    }

    /* Route events to the appropriate window(s) */

    /* For update events, route to the specific window referenced in the event */
    if (event->what == updateEvt) {
        WindowRef window = (WindowRef)event->message;

        if (window == SplashWindow_GetWindowRef() && SplashWindow_IsVisible()) {
            SplashWindow_HandleEvent(event);
            return;
        }
        else if (window == ChatWindow_GetWindowRef() && ChatWindow_IsVisible()) {
            ChatWindow_HandleEvent(event);
            return;
        }
        else if (window == AboutWindow_GetWindowRef() && AboutWindow_IsVisible()) {
            AboutWindow_HandleEvent(event);
            return;
        }
    }

    /* For other events, route based on the foreground window */
    switch (gForegroundWindowType) {
    case kWindowTypeSplash:
        SplashWindow_HandleEvent(event);
        break;

    case kWindowTypeChat:
        ChatWindow_HandleEvent(event);
        break;

    case kWindowTypeAbout:
        AboutWindow_HandleEvent(event);
        break;
    }
}

/* Perform idle-time processing */
void WindowManager_Idle(void)
{
    /* Pass idle time to the active window */
    switch (gForegroundWindowType) {
    case kWindowTypeSplash:
        /* Splash window doesn't need idle processing */
        break;

    case kWindowTypeChat:
        ChatWindow_Idle();
        break;

    default:
        /* No idle processing for other windows */
        break;
    }
}