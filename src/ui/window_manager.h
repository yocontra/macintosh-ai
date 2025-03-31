#ifndef WINDOW_MANAGER_H
#define WINDOW_MANAGER_H

#include <Events.h>
#include <Windows.h>

/*
 * Window manager module that coordinates between individual window implementations.
 * Centralizes window state management and sends events to the active window.
 * Each window implements its own event handling logic.
 */

/* Window type identifiers */
typedef enum { kWindowTypeSplash = 0, kWindowTypeChat = 1, kWindowTypeAbout = 2 } WindowType;

/* Window management functions */
void WindowManager_Initialize(void);
void WindowManager_Dispose(void);

/* Window operations */
void WindowManager_OpenWindow(WindowType windowType);
void WindowManager_CloseWindow(WindowType windowType);
void WindowManager_SetForegroundWindow(WindowType windowType);
WindowType WindowManager_GetForegroundWindowType(void);
WindowRef WindowManager_GetWindowRef(WindowType windowType);
Boolean WindowManager_IsWindowVisible(WindowType windowType);

/* Event handling */
void WindowManager_HandleEvent(EventRecord *event);
void WindowManager_Idle(void);

#endif /* WINDOW_MANAGER_H */