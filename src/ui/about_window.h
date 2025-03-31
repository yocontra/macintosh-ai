#ifndef ABOUT_WINDOW_H
#define ABOUT_WINDOW_H

#include <Events.h>

/* Show the About Box dialog */
void ShowAboutBox(void);

/* Handle events for the about box (if visible) */
void AboutWindow_HandleEvent(EventRecord *event);

#endif /* ABOUT_WINDOW_H */