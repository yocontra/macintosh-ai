#ifndef CHAT_WINDOW_H
#define CHAT_WINDOW_H

#include <Controls.h>
#include <TextEdit.h>
#include <Windows.h>

/* Initialize and create the chat window */
void ChatWindow_Initialize(void);

/* Dispose of the chat window and clean up resources */
void ChatWindow_Dispose(void);

/* Handle all events for the chat window */
void ChatWindow_HandleEvent(EventRecord *event);

/* Get the active TEHandle for text operations */
TEHandle ChatWindow_GetActiveTE(void);

/* Determine if the display area has a selection */
Boolean ChatWindow_HasDisplaySelection(void);

/* Determine if the input area has a selection */
Boolean ChatWindow_HasInputSelection(void);

/* Perform a Cut operation on the appropriate TE */
void ChatWindow_CutText(void);

/* Perform a Copy operation on the appropriate TE */
void ChatWindow_CopyText(void);

/* Perform a Paste operation on the input TE */
void ChatWindow_PasteText(void);

/* Perform a Clear/Delete operation on the appropriate TE */
void ChatWindow_ClearText(void);


/* Get the window reference for the chat window */
WindowRef ChatWindow_GetWindowRef(void);

/* Show or hide the chat window */
void ChatWindow_Show(Boolean visible);

/* Determine if the window is visible */
Boolean ChatWindow_IsVisible(void);

/* Add a message to the chat display */
void ChatWindow_AddMessage(const char *message, Boolean isUserMessage);

/* Send a message from the chat input field */
void ChatWindow_SendMessage(void);

/* Perform idle processing (text cursor blinking, etc.) */
void ChatWindow_Idle(void);

/* Toggle between AI models */
void ChatWindow_ToggleAIModel(void);

#endif /* CHAT_WINDOW_H */