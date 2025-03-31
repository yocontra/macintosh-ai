#ifndef SPLASH_WINDOW_H
#define SPLASH_WINDOW_H

#include <Events.h>
#include <Quickdraw.h>
#include <Windows.h>

/* Splash Window API */

/* Initialize and create the splash window */
void SplashWindow_Initialize(void);

/* Dispose of the splash window and clean up resources */
void SplashWindow_Dispose(void);

/* Handle all events for the splash window */
void SplashWindow_HandleEvent(EventRecord *event);

/* Render the splash window contents */
void SplashWindow_Render(void);

/* Get the window reference for the splash window */
WindowRef SplashWindow_GetWindowRef(void);

/* Show or hide the splash window */
void SplashWindow_Show(Boolean visible);

/* Determine if the window is visible */
Boolean SplashWindow_IsVisible(void);

#endif /* SPLASH_WINDOW_H */