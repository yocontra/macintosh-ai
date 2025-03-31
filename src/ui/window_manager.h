#ifndef WINDOW_MANAGER_H
#define WINDOW_MANAGER_H

#include <Events.h>
#include <Windows.h>

/* Window types */
enum { kWindowTypeSplash = 0, kWindowTypeChat = 1, kWindowTypeAbout = 2 };
typedef unsigned short WindowType;

/* Common window module interface */
typedef struct {
    WindowRef window;
    Boolean initialized;
    Boolean visible;
    void (*initialize)(void);
    void (*dispose)(void);
    void (*show)(Boolean);
    void (*handleEvent)(EventRecord *);
    void (*update)(void);
    WindowRef (*getWindowRef)(void);
    Boolean (*isVisible)(void);
} WindowModule;

/* Initialize the window manager and the initial window */
void WindowManager_Initialize(void);

/* Dispose all windows and clean up resources */
void WindowManager_Dispose(void);

/* Open a specific type of window */
void WindowManager_OpenWindow(WindowType windowType);

/* Close a specific type of window */
void WindowManager_CloseWindow(WindowType windowType);

/* Set the foreground window type and update application state */
void WindowManager_SetForegroundWindow(WindowType windowType);

/* Get the current foreground window type */
WindowType WindowManager_GetForegroundWindowType(void);

/* Get window reference for a specific window type */
WindowRef WindowManager_GetWindowRef(WindowType windowType);

/* Check if a window is visible */
Boolean WindowManager_IsWindowVisible(WindowType windowType);

/* Handle all event types and route to appropriate handler */
void WindowManager_HandleEvent(EventRecord *event);

/* Perform idle-time processing */
void WindowManager_Idle(void);

#endif /* WINDOW_MANAGER_H */