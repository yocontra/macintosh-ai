#ifndef AI_MODEL_H
#define AI_MODEL_H

#include "constants.h"

/* Maximum number of conversation turns we'll track */
#define kMaxConversationHistory 50

/* Message type enumeration */
typedef enum { kUserMessage = 0, kAIMessage = 1 } MessageType;

/* Structure for a single message in the conversation */
typedef struct {
    MessageType type;            /* Who sent the message */
    char text[kMaxPromptLength]; /* Content of the message */
} ConversationMessage;

/* Structure for the entire conversation history */
typedef struct {
    ConversationMessage messages[kMaxConversationHistory];
    short count; /* Number of messages in the history */
} ConversationHistory;

/* Global conversation history */
extern ConversationHistory gConversationHistory;

/* Initialize the conversation history */
void InitConversationHistory(void);

/* Interface for AI model interaction */
char *GenerateAIResponse(const ConversationHistory *history);

/* Add a user prompt to the conversation */
void AddUserPrompt(const char *prompt);

/* Add an AI response to the conversation */
void AddAIResponse(const char *response);

#endif /* AI_MODEL_H */