#ifndef CHAT_WINDOW_H
#define CHAT_WINDOW_H

#include <Quickdraw.h>
#include <Windows.h>

/* Chat Window API */

/* Initialize and create the chat window */
void ChatWindow_Initialize(void);

/* Dispose of the chat window and clean up resources */
void ChatWindow_Dispose(void);

/* Handle update events for the chat window */
void ChatWindow_Update(void);

/* Handle mouse clicks in the content area of the chat window */
Boolean ChatWindow_HandleContentClick(Point localPt);

/* Handle key events in the chat window */
Boolean ChatWindow_HandleKeyDown(char key, Boolean isShiftDown, Boolean isCmdDown);

/* Handle window activation/deactivation events */
void ChatWindow_HandleActivate(Boolean becomingActive);

/* Get the window reference for the chat window */
WindowRef ChatWindow_GetWindowRef(void);

/* Get the display TextEdit handle for external operations */
TEHandle ChatWindow_GetDisplayTE(void);

/* Get the input TextEdit handle for external operations */
TEHandle ChatWindow_GetInputTE(void);

/* Show or hide the chat window */
void ChatWindow_Show(Boolean visible);

/* Determine if the window is visible */
Boolean ChatWindow_IsVisible(void);

/* Process Return key in the chat input field */
void ChatWindow_ProcessReturnKey(void);

/* Add a message to the chat display */
void ChatWindow_AddMessage(const char *message, Boolean isUserMessage);

/* Perform idle processing (text cursor blinking, etc.) */
void ChatWindow_Idle(void);

#endif /* CHAT_WINDOW_H */