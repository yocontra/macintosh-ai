#include <Memory.h>
#include <OSUtils.h>
#include <TextEdit.h>
#include <stdio.h>
#include <string.h>

#include "ai_model.h"
#include "constants.h"

/*********************************************************************
 * AI MODEL INTERFACE
 *********************************************************************/

/* Global conversation history */
ConversationHistory gConversationHistory;

/* Initialize the conversation history */
void InitConversationHistory(void)
{
    /* Clear the conversation history */
    gConversationHistory.count = 0;
    memset(gConversationHistory.messages, 0, sizeof(ConversationMessage) * kMaxConversationHistory);

    /* Initialize the global buffer to empty string to prevent crashes */
    if (gConversationBuffer != NULL) {
        gConversationBuffer[0] = '\0';
    }
}

/* Function that returns an appropriate response based on user input */
char *GenerateAIResponse(const ConversationHistory *history)
{
    /* In Phase 4, this would connect to an actual AI model */
    /* For now, just return a simple response */
    static char response[512];

    /* Simple response - no errors */
    strcpy(response, "This is a test placeholder response from the AI model.");

    if (history != NULL && history->count > 0) {
        /* Find the last user message */
        short i;
        for (i = history->count - 1; i >= 0; i--) {
            if (history->messages[i].type == kUserMessage) {
                const char *prompt = history->messages[i].text;

                /* Get current date/time */
                DateTimeRec dateTime;
                GetTime(&dateTime);
                char dateStr[64];

                /* Pattern matching for different types of questions */
                if ((strstr(prompt, "what") || strstr(prompt, "What")) &&
                    strstr(prompt, "current month")) {
                    /* Month names */
                    const char *monthNames[] = {"January",   "February", "March",    "April",
                                                "May",       "June",     "July",     "August",
                                                "September", "October",  "November", "December"};
                    sprintf(response, "The current month is %s %d.", monthNames[dateTime.month - 1],
                            dateTime.year);
                }
                else if ((strstr(prompt, "what") || strstr(prompt, "What")) &&
                         (strstr(prompt, "ai") || strstr(prompt, "AI"))) {
                    strcpy(response, "AI (Artificial Intelligence) is the simulation of human "
                                     "intelligence by machines.");
                }
                else if (strstr(prompt, "hello") || strstr(prompt, "hi") ||
                         strstr(prompt, "Hello") || strstr(prompt, "Hi")) {
                    strcpy(response,
                           "Hello! I'm an AI assistant for Macintosh. How can I help you today?");
                }
                else if (strstr(prompt, "help") || strstr(prompt, "Help")) {
                    strcpy(response,
                           "I can answer questions, provide information, and help with various "
                           "tasks. Just type your question and press Return.");
                }
                else {
                    strcpy(
                        response,
                        "That's an interesting question. In the future, I'll connect to an actual "
                        "AI model to provide more helpful responses.");
                }

                break;
            }
        }
    }

    return response;
}

/* Add a user prompt to the conversation */
void AddUserPrompt(const char *prompt)
{
    if (gConversationHistory.count < kMaxConversationHistory) {
        /* Add to the structured conversation history */
        short idx                               = gConversationHistory.count++;
        gConversationHistory.messages[idx].type = kUserMessage;
        strncpy(gConversationHistory.messages[idx].text, prompt, kMaxPromptLength - 1);
        gConversationHistory.messages[idx].text[kMaxPromptLength - 1] = '\0';
    }
}

/* Add an AI response to the conversation */
void AddAIResponse(const char *response)
{
    if (gConversationHistory.count < kMaxConversationHistory) {
        /* Add to the structured conversation history */
        short idx                               = gConversationHistory.count++;
        gConversationHistory.messages[idx].type = kAIMessage;
        strncpy(gConversationHistory.messages[idx].text, response, kMaxPromptLength - 1);
        gConversationHistory.messages[idx].text[kMaxPromptLength - 1] = '\0';
    }
}
