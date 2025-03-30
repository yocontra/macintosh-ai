#ifndef SPLASH_WINDOW_H
#define SPLASH_WINDOW_H

#include <Quickdraw.h>
#include <Windows.h>

/* Splash Window API */

/* Initialize and create the splash window */
void SplashWindow_Initialize(void);

/* Dispose of the splash window and clean up resources */
void SplashWindow_Dispose(void);

/* Handle update events for the splash window */
void SplashWindow_Update(void);

/* Handle mouse clicks in the content area of the splash window */
Boolean SplashWindow_HandleContentClick(Point localPt);

/* Get the window reference for the splash window */
WindowRef SplashWindow_GetWindowRef(void);

/* Show or hide the splash window */
void SplashWindow_Show(Boolean visible);

/* Determine if the window is visible */
Boolean SplashWindow_IsVisible(void);

#endif /* SPLASH_WINDOW_H */