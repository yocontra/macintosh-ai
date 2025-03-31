#ifndef MODEL_MANAGER_H
#define MODEL_MANAGER_H

#include "markov.h"

/* AI Model types */
typedef enum { kMarkovModel = 0, kOpenAIModel = 1, kTemplateModel = 2 } AIModelType;

/* Global conversation history */
extern ConversationHistory gConversationHistory;

/* Global to track which AI model is currently active */
extern AIModelType gActiveAIModel;

/* Initialize AI models and conversation history */
void InitModels(void);

/* Set the active AI model */
void SetActiveAIModel(AIModelType modelType);

/* Generate AI response based on active model */
char *GenerateAIResponse(const ConversationHistory *history);

/* Add a user prompt to the conversation */
void AddUserPrompt(const char *prompt);

/* Add an AI response to the conversation */
void AddAIResponse(const char *response);

#endif /* MODEL_MANAGER_H */