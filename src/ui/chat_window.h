#ifndef CHAT_WINDOW_H
#define CHAT_WINDOW_H

#include <Events.h>
#include <Quickdraw.h>
#include <Windows.h>

/* Chat Window API */

/* Initialize and create the chat window */
void ChatWindow_Initialize(void);

/* Dispose of the chat window and clean up resources */
void ChatWindow_Dispose(void);

/* Handle all events for the chat window */
void ChatWindow_HandleEvent(EventRecord *event);

/* Handle update events for the chat window */
void ChatWindow_Update(void);

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

/* Send a message from the chat input field */
void ChatWindow_SendMessage(void);

/* Add a message to the chat display */
void ChatWindow_AddMessage(const char *message, Boolean isUserMessage);

/* Perform idle processing (text cursor blinking, etc.) */
void ChatWindow_Idle(void);

/* Toggle between AI models */
void ChatWindow_ToggleAIModel(void);

#endif /* CHAT_WINDOW_H */