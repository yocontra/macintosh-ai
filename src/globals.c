#include "constants.h"

/*********************************************************************
 * GLOBAL VARIABLES
 *********************************************************************/

/* Application state is now defined in main.c */

/* Main window state */
WindowRef gMainWindow          = NULL;
ControlHandle gStartChatButton = NULL;

/* Chat window state */
WindowRef gChatWindow   = NULL;
Boolean gChatWindowOpen = false;
Rect gChatContentRect;                        /* Main content area */
Rect gChatInputRect;                          /* Input field at bottom */
Rect gChatDisplayRect;                        /* Display area for conversation */
TEHandle gChatInputTE        = NULL;          /* Text edit handle for input */
TEHandle gChatDisplayTE      = NULL;          /* Text edit handle for chat display */
ControlHandle gChatScrollBar = NULL;          /* Scrollbar for chat display */
char gPromptBuffer[kMaxPromptLength];         /* Buffer for user prompt */
char gConversationBuffer[kMaxResponseLength]; /* Buffer for entire conversation */
