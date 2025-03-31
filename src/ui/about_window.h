#ifndef ABOUT_WINDOW_H
#define ABOUT_WINDOW_H

#include <Events.h>
#include <Quickdraw.h>
#include <Windows.h>

/* About Window API */

/* Initialize and create the about window */
void AboutWindow_Initialize(void);

/* Dispose of the about window and clean up resources */
void AboutWindow_Dispose(void);

/* Handle all events for the about window */
void AboutWindow_HandleEvent(EventRecord *event);

/* Render the about window contents */
void AboutWindow_Render(void);

/* Get the window reference for the about window */
WindowRef AboutWindow_GetWindowRef(void);

/* Show or hide the about window */
void AboutWindow_Show(Boolean visible);

/* Determine if the window is visible */
Boolean AboutWindow_IsVisible(void);

#endif /* ABOUT_WINDOW_H */