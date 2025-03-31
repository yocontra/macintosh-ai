#ifndef MARKOV_H
#define MARKOV_H

#include "../constants.h"

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

/* Train the Markov chain with new text */
void TrainMarkov(const char *text);

/* Load the training data for the Markov model */
void LoadTrainingData(void);

/* Initialize the Markov model */
void InitMarkovModel(void);

/* Interface for Markov model interaction */
char *GenerateMarkovResponse(const ConversationHistory *history);

#endif /* MARKOV_H */