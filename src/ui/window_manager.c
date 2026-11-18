#include <Devices.h>
#include <Events.h>
#include <Menus.h>
#include <Windows.h>

#include "../chatbot/api_client.h"
#include "../constants.h"
#include "../error.h"
#include "chat_window.h"
#include "event.h"
#include "menu.h"
#include "splash_window.h"
#include "window_manager.h"

/*
 * Window Manager module that coordinates operations between
 * individual window modules using a standardized interface.
 *
 * The About box is not a window module: it is a modal dialog shown and
 * disposed entirely inside AboutWindow_Show().
 */

#define kWindowModuleCount 2

/* Window modules registration */
static WindowModule gWindowModules[kWindowModuleCount];

/* Current active window tracking */
static WindowType gForegroundWindowType = kWindowTypeSplash;

/* Find the module that owns a window, if any */
static short FindModuleForWindow(WindowRef window)
{
    short i;

    if (window == NULL) {
        return -1;
    }

    for (i = 0; i < kWindowModuleCount; i++) {
        if (gWindowModules[i].initialized && gWindowModules[i].getWindowRef() == window) {
            return i;
        }
    }

    return -1;
}

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
}

/* Initialize the window manager and the initial window */
Boolean WindowManager_Initialize(void)
{
    RegisterWindowModules();

    /* Initialize splash window as the default view */
    if (!gWindowModules[kWindowTypeSplash].initialize()) {
        return false;
    }
    gWindowModules[kWindowTypeSplash].initialized = true;

    if (!gWindowModules[kWindowTypeSplash].show(true)) {
        return false;
    }
    gWindowModules[kWindowTypeSplash].visible = true;

    /* Set the splash window as foreground so menus start in the right state */
    WindowManager_SetForegroundWindow(kWindowTypeSplash);

    return true;
}

/* Dispose all windows and clean up resources */
void WindowManager_Dispose(void)
{
    short i;

    /* Dispose windows that were initialized */
    for (i = 0; i < kWindowModuleCount; i++) {
        if (gWindowModules[i].initialized) {
            gWindowModules[i].dispose();
            gWindowModules[i].initialized = false;
            gWindowModules[i].visible     = false;
        }
    }
}

/* Open a specific type of window */
Boolean WindowManager_OpenWindow(WindowType windowType)
{
    if (windowType >= kWindowModuleCount) {
        return false;
    }

    /* Initialize the window if it hasn't been initialized yet */
    if (!gWindowModules[windowType].initialized) {
        if (!gWindowModules[windowType].initialize()) {
            return false;
        }
        gWindowModules[windowType].initialized = true;
    }

    /* Show the window */
    if (!gWindowModules[windowType].show(true)) {
        return false;
    }
    gWindowModules[windowType].visible = true;

    WindowManager_SetForegroundWindow(windowType);

    return true;
}

/* Close a specific type of window */
void WindowManager_CloseWindow(WindowType windowType)
{
    /* Only hide the window if it was initialized */
    if (windowType < kWindowModuleCount && gWindowModules[windowType].initialized) {
        gWindowModules[windowType].show(false);
        gWindowModules[windowType].visible = false;
    }
}

/* Set the foreground window type and update application state */
void WindowManager_SetForegroundWindow(WindowType windowType)
{
    WindowRef windowRef;

    /* Ignore if the window hasn't been initialized */
    if (windowType >= kWindowModuleCount || !gWindowModules[windowType].initialized) {
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
    if (windowType < kWindowModuleCount && gWindowModules[windowType].initialized) {
        return gWindowModules[windowType].getWindowRef();
    }
    return NULL;
}

/* Check if a window is visible */
Boolean WindowManager_IsWindowVisible(WindowType windowType)
{
    if (windowType < kWindowModuleCount && gWindowModules[windowType].initialized) {
        return gWindowModules[windowType].isVisible();
    }
    return false;
}

/* Close the chat window and go back to the splash screen */
static void ReturnToSplash(void)
{
    WindowManager_CloseWindow(kWindowTypeChat);

    if (!WindowManager_OpenWindow(kWindowTypeSplash)) {
        HandleError(kErrWindowCreation, kCtxCreatingMainWindow, true);
    }
}

/* Handle standard window operations like dragging, closing, etc. */
static Boolean HandleStandardWindowOps(EventRecord *event)
{
    WindowRef window;
    short part;

    if (event->what != mouseDown) {
        return false;
    }

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
                API_Shutdown();
                QuitApplication(false);
            }
            else if (window == gWindowModules[kWindowTypeChat].getWindowRef()) {
                /* Closing chat window returns to splash screen */
                ReturnToSplash();
            }
            else {
                DisposeWindow(window);
            }
        }
        return true;
    }
    else if (part == inContent && window != FrontWindow()) {
        /* Activate clicked window */
        short module = FindModuleForWindow(window);

        SelectWindow(window);
        if (module >= 0) {
            WindowManager_SetForegroundWindow((WindowType)module);
        }
        return true;
    }

    return false;
}

/* Handle window activation events */
static void HandleWindowActivation(EventRecord *event)
{
    WindowRef window;
    Boolean becomingActive;

    if (event->what != activateEvt) {
        return;
    }

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

/* Route update events to the appropriate window */
static Boolean RouteUpdateEvent(EventRecord *event)
{
    WindowRef window;
    short module;

    if (event->what != updateEvt) {
        return false;
    }

    window = (WindowRef)event->message;
    module = FindModuleForWindow(window);

    if (module < 0) {
        return false;
    }

    if (gWindowModules[module].isVisible()) {
        gWindowModules[module].handleEvent(event);
    }
    else {
        /* Balance BeginUpdate/EndUpdate even for a hidden window */
        GrafPtr savePort;

        GetPort(&savePort);
        SetPort(window);
        BeginUpdate(window);
        EndUpdate(window);
        SetPort(savePort);
    }

    return true;
}

/* Handle all event types and route to appropriate handler */
void WindowManager_HandleEvent(EventRecord *event)
{
    /* Handle standard window operations first */
    if (HandleStandardWindowOps(event)) {
        return;
    }

    /* Handle window activation events */
    HandleWindowActivation(event);

    /* Route update events to specific windows */
    if (RouteUpdateEvent(event)) {
        return;
    }

    /* Route other events to the foreground window */
    if (gForegroundWindowType < kWindowModuleCount &&
        gWindowModules[gForegroundWindowType].initialized &&
        gWindowModules[gForegroundWindowType].visible) {
        gWindowModules[gForegroundWindowType].handleEvent(event);
    }
}
