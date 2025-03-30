#include <Fonts.h>
#include <Memory.h>
#include <Quickdraw.h>
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ai_model.h"
#include "../constants.h"
#include "../error.h"
#include "../nuklear/nuklear.h" /* Include only header without implementation */
#include "chat_window.h"

/* External function declarations instead of including nuklear_quickdraw.h */
extern struct nk_context* nk_quickdraw_init(unsigned int width, unsigned int height);
extern int nk_quickdraw_handle_event(EventRecord *event, struct nk_context *nuklear_context);
extern void nk_quickdraw_render(WindowPtr window, struct nk_context *ctx);
extern void nk_quickdraw_shutdown(void);

/* External references to the shared nuklear context */
extern struct nk_context *ctx;

/* Message display buffers */
#define MAX_DISPLAYED_MESSAGES 20
static char sMessageBuffer[MAX_DISPLAYED_MESSAGES][kMaxPromptLength];
static short sMessageTypes[MAX_DISPLAYED_MESSAGES]; /* 0 = user, 1 = AI */
static short sMessageCount = 0;

/* Private module variables */
static WindowRef sWindow        = NULL;
static Boolean sInitialized     = false;
static Boolean sIsVisible       = false;
static char sPromptBuffer[kMaxPromptLength];
static short sPromptBufferLen = 0;
static Boolean sForceRedraw   = true;

/* Function declarations */
void nuklearApp_Chat(struct nk_context *ctx);
void refreshChatApp(Boolean blankInput);

/* The main Nuklear UI function for the chat window 
 * This follows the pattern from nuklear_calculator.c
 */
void nuklearApp_Chat(struct nk_context *ctx)
{
    int i;

    /* Chat messages area - create a window with scrollable content */
    if (nk_begin(ctx, "AI Chat", nk_rect(10, 10, 500, 300),
        NK_WINDOW_BORDER|NK_WINDOW_NO_SCROLLBAR|NK_WINDOW_MOVABLE)) {
                
        /* Iterate through all messages and display them */
        for (i = 0; i < sMessageCount; i++) {
            int isUserMessage = (sMessageTypes[i] == 0);

            /* Create rows for each message with proper alignment */
            if (isUserMessage) {
                /* Right-aligned for user messages */
                nk_layout_row_begin(ctx, NK_STATIC, 25, 2);
                nk_layout_row_push(ctx, 100); /* Spacing */
                nk_spacing(ctx, 1);
                nk_layout_row_push(ctx, 380); /* Message width */
                nk_label(ctx, sMessageBuffer[i],
                        NK_TEXT_LEFT); /* Use NK_TEXT_LEFT for now as we don't have wrap */
                nk_layout_row_end(ctx);
            }
            else {
                /* Left-aligned for AI messages */
                nk_layout_row_begin(ctx, NK_STATIC, 25, 2);
                nk_layout_row_push(ctx, 380); /* Message width */
                nk_label(ctx, sMessageBuffer[i],
                        NK_TEXT_LEFT);       /* Use NK_TEXT_LEFT for now as we don't have wrap */
                nk_layout_row_push(ctx, 100); /* Spacing */
                nk_spacing(ctx, 1);
                nk_layout_row_end(ctx);
            }

            /* Add spacing between messages */
            nk_layout_row_dynamic(ctx, 10, 1);
            nk_spacing(ctx, 1);
        }

        /* If new messages were added, scroll to the bottom */
        if (sForceRedraw && sMessageCount > 0) {
            short scroll_x = 0, scroll_y = 0;
            nk_window_get_scroll(ctx, &scroll_x, &scroll_y);
            nk_window_set_scroll(ctx, scroll_x,
                                32000); /* Large value to ensure it scrolls to bottom */
            sForceRedraw = false;
        }

        nk_end(ctx);
    }

    /* Input area at the bottom */
    if (nk_begin(ctx, "Input", nk_rect(10, 320, 500, 40), 
                NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
                
        /* Message input field and send button layout */
        nk_layout_row_begin(ctx, NK_STATIC, 30, 2);
        nk_layout_row_push(ctx, 420); /* Text input width */

        /* Text input with Enter key handling */
        nk_flags result =
            nk_edit_string(ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER, sPromptBuffer, &sPromptBufferLen,
                           (int)(kMaxPromptLength - 1), nk_filter_default);

        /* Check for Enter key */
        if (result & NK_EDIT_COMMITED && sPromptBufferLen > 0) {
            /* Process the message */
            ChatWindow_ProcessReturnKey();
        }

        /* Send button */
        nk_layout_row_push(ctx, 60);
        if (nk_button_label(ctx, "Send") && sPromptBufferLen > 0) {
            /* Process the message */
            ChatWindow_ProcessReturnKey();
        }

        nk_layout_row_end(ctx);
        nk_end(ctx);
    }
}

/* Refresh the chat app - follows pattern from nuklear_calculator.c */
void refreshChatApp(Boolean blankInput)
{
    if (!sInitialized || !sIsVisible || sWindow == NULL)
        return;
        
    SetPort(sWindow);
    
    nk_input_begin(ctx);
    
    if (blankInput) {
        nk_input_key(ctx, NK_KEY_DEL, 1);
        nk_input_key(ctx, NK_KEY_DEL, 0);
    }
    
    nk_input_end(ctx);
    nuklearApp_Chat(ctx);
    nk_quickdraw_render(sWindow, ctx);
    nk_clear(ctx);
}

/* Initialize and create the chat window */
void ChatWindow_Initialize(void)
{
    Rect windowRect;
    short screenWidth, screenHeight;
    short windowWidth, windowHeight;
    OSErr err;

    /* Prevent double initialization */
    if (sInitialized)
        return;

    /* Initialize conversation history if needed */
    InitConversationHistory();

    /* Check available memory first */
    err = CheckMemory();
    if (err != noErr) {
        HandleError(kErrMemoryFull, kCtxOpeningChatWindow, false); /* Non-fatal */
        return;
    }

    /* Calculate a window size that fits well on the Macintosh Plus screen */
    screenWidth  = qd.screenBits.bounds.right - qd.screenBits.bounds.left;
    screenHeight = qd.screenBits.bounds.bottom - qd.screenBits.bounds.top;

    /* Make sure the chat window is not too big (max 80% of screen) */
    windowWidth  = (screenWidth * 4 / 5);
    windowHeight = screenHeight - 80;

    SetRect(&windowRect, (screenWidth - windowWidth) / 2, /* Center horizontally */
            40,                                           /* Top margin */
            (screenWidth - windowWidth) / 2 + windowWidth, 40 + windowHeight); /* Bottom margin */

    /* Create the window */
    sWindow = NewWindow(NULL, &windowRect, "\pAI Chat", true, documentProc, (WindowPtr)-1, true, 0);

    if (sWindow == NULL) {
        HandleError(kErrWindowCreation, kCtxOpeningChatWindow, false); /* Non-fatal */
        return;
    }

    SetPort(sWindow);

    /* Clear the prompt buffer */
    memset(sPromptBuffer, 0, kMaxPromptLength);
    sPromptBufferLen = 0;

    /* Clear message buffers */
    memset(sMessageBuffer, 0, sizeof(sMessageBuffer));
    memset(sMessageTypes, 0, sizeof(sMessageTypes));
    sMessageCount = 0;

    sInitialized = true;
    sIsVisible   = false;
    sForceRedraw = true;
    
    /* Explicitly hide the window to ensure it's not visible on initialization */
    HideWindow(sWindow);
}

/* Dispose of the chat window and clean up resources */
void ChatWindow_Dispose(void)
{
    if (!sInitialized) {
        return;
    }

    /* Dispose the window */
    if (sWindow != NULL) {
        DisposeWindow(sWindow);
        sWindow = NULL;
    }

    sInitialized = false;
    sIsVisible   = false;
}

/* Handle update events for the chat window */
void ChatWindow_Update(void)
{
    if (!sInitialized || sWindow == NULL || !sIsVisible)
        return;

    SetPort(sWindow);
    BeginUpdate(sWindow);

    refreshChatApp(false);

    EndUpdate(sWindow);
}

/* Handle mouse clicks in the content area of the chat window */
Boolean ChatWindow_HandleContentClick(Point localPt)
{
    if (!sInitialized || sWindow == NULL || !sIsVisible)
        return false;

    /* The actual event handling is done in main.c through nk_quickdraw_handle_event */
    /* We just need to make sure the window is ready for user input */
    refreshChatApp(false);
    return true;
}

/* Process Return key in the chat input field */
void ChatWindow_ProcessReturnKey(void)
{
    char promptBuffer[kMaxPromptLength];
    char thinkingMsg[80];
    char *response;

    /* Early return for invalid state */
    if (!sInitialized || sWindow == NULL || sPromptBufferLen <= 0)
        return;

    /* Copy the prompt buffer */
    strncpy(promptBuffer, sPromptBuffer, kMaxPromptLength - 1);
    promptBuffer[kMaxPromptLength - 1] = '\0';

    /* Clear the input field */
    memset(sPromptBuffer, 0, kMaxPromptLength);
    sPromptBufferLen = 0;

    /* Add user prompt to conversation history */
    AddUserPrompt(promptBuffer);

    /* Add the user message to display */
    ChatWindow_AddMessage(promptBuffer, true); /* true = user message */

    /* Add temporary "Thinking..." indicator */
    sprintf(thinkingMsg, "Processing your query...");
    ChatWindow_AddMessage(thinkingMsg, false); /* false = AI message */

    /* Generate AI response */
    response = GenerateAIResponse(&gConversationHistory);

    /* Add AI response to conversation history */
    AddAIResponse(response);

    /* Replace the "thinking" message with the actual response */
    if (sMessageCount > 0 && sMessageTypes[sMessageCount - 1] == 1) {
        sMessageCount--; /* Remove the thinking message */
    }

    /* Add the actual response */
    ChatWindow_AddMessage(response, false);

    /* Force a redraw to show the newest messages */
    sForceRedraw = true;
    refreshChatApp(true);
}

/* Handle key events in the chat window */
Boolean ChatWindow_HandleKeyDown(char key, Boolean isShiftDown, Boolean isCmdDown)
{
    if (!sInitialized || sWindow == NULL || !sIsVisible)
        return false;

    /* The actual key handling is done in main.c through nk_quickdraw_handle_event */
    /* But handle Return key specifically here */
    if (key == '\r' || key == '\n' || key == 3) {
        if (!isShiftDown && sPromptBufferLen > 0) {
            /* Regular Return: Submit prompt and generate response */
            ChatWindow_ProcessReturnKey();
            return true;
        }
    }

    return false;
}

/* Handle window activation/deactivation events */
void ChatWindow_HandleActivate(Boolean becomingActive)
{
    if (!sInitialized || sWindow == NULL)
        return;

    if (becomingActive) {
        /* Force update when activated */
        sForceRedraw = true;
        refreshChatApp(false);
    }
}

/* Get the window reference for the chat window */
WindowRef ChatWindow_GetWindowRef(void)
{
    return sWindow;
}

/* Get the display TextEdit handle for external operations - Not relevant with Nuklear */
TEHandle ChatWindow_GetDisplayTE(void)
{
    return NULL;
}

/* Get the input TextEdit handle for external operations - Not relevant with Nuklear */
TEHandle ChatWindow_GetInputTE(void)
{
    return NULL;
}

/* Refresh the conversation history display */
static void RefreshConversationDisplay(void)
{
    short i;

    /* Safety check */
    if (!sInitialized)
        return;

    /* Clear the message buffer */
    sMessageCount = 0;
    memset(sMessageBuffer, 0, sizeof(sMessageBuffer));
    memset(sMessageTypes, 0, sizeof(sMessageTypes));

    /* Display all messages from the conversation history */
    for (i = 0; i < gConversationHistory.count && i < MAX_DISPLAYED_MESSAGES; i++) {
        ConversationMessage *msg = &gConversationHistory.messages[i];
        if (msg->text[0] != '\0') {
            strncpy(sMessageBuffer[sMessageCount], msg->text, kMaxPromptLength - 1);
            sMessageBuffer[sMessageCount][kMaxPromptLength - 1] = '\0';
            sMessageTypes[sMessageCount] = (msg->type == kUserMessage) ? 0 : 1;
            sMessageCount++;
        }
    }

    /* Force redraw */
    sForceRedraw = true;
    refreshChatApp(false);
}

/* Show or hide the chat window */
void ChatWindow_Show(Boolean visible)
{
    if (!sInitialized || sWindow == NULL)
        return;

    if (visible) {
        /* Refresh conversation display when showing window */
        ShowWindow(sWindow);
        SelectWindow(sWindow);
        sIsVisible = true;
        
        RefreshConversationDisplay();

        /* Force update when shown */
        sForceRedraw = true;
        refreshChatApp(false);
        return;
    }

    HideWindow(sWindow);
    sIsVisible = false;
}

/* Determine if the window is visible */
Boolean ChatWindow_IsVisible(void)
{
    if (!sInitialized || sWindow == NULL) {
        return false;
    }

    return sIsVisible;
}

/* Add a message to the chat display */
void ChatWindow_AddMessage(const char *message, Boolean isUserMessage)
{
    if (!sInitialized || sWindow == NULL || message == NULL || strlen(message) == 0) {
        return;
    }

    /* Safety check for buffer overflow */
    if (sMessageCount >= MAX_DISPLAYED_MESSAGES) {
        /* Shift messages to make room for new one */
        int i;
        for (i = 0; i < MAX_DISPLAYED_MESSAGES - 1; i++) {
            strcpy(sMessageBuffer[i], sMessageBuffer[i + 1]);
            sMessageTypes[i] = sMessageTypes[i + 1];
        }
        sMessageCount = MAX_DISPLAYED_MESSAGES - 1;
    }

    /* Add message to buffer */
    strncpy(sMessageBuffer[sMessageCount], message, kMaxPromptLength - 1);
    sMessageBuffer[sMessageCount][kMaxPromptLength - 1] = '\0';
    sMessageTypes[sMessageCount]                        = isUserMessage ? 0 : 1;
    sMessageCount++;

    /* Force redraw */
    sForceRedraw = true;
    refreshChatApp(false);
}

/* Perform idle processing */
void ChatWindow_Idle(void)
{
    if (!sInitialized || sWindow == NULL || !sIsVisible) {
        return;
    }
}