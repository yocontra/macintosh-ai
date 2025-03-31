#include <Memory.h>
#include <stdio.h>
#include <string.h>

#include "../constants.h"
#include "markov.h"
#include "model_manager.h"
#include "openai.h"

/* Global conversation history */
ConversationHistory gConversationHistory;

/* Default to Markov model */
AIModelType gActiveAIModel = kMarkovModel;

/* Initialize all AI models and conversation history */
void InitModels(void)
{
    /* Clear the conversation history */
    gConversationHistory.count = 0;
    memset(gConversationHistory.messages, 0, sizeof(ConversationMessage) * kMaxConversationHistory);

    /* Initialize the Markov model */
    if (gActiveAIModel == kMarkovModel) {
        InitMarkovModel();
    }
    else if (gActiveAIModel == kOpenAIModel) {
        InitOpenAI();
    }
}

/* Set the active AI model */
void SetActiveAIModel(AIModelType modelType)
{
    gActiveAIModel = modelType;
}

/* Generate AI response based on active model */
char *GenerateAIResponse(const ConversationHistory *history)
{
    if (gActiveAIModel == kMarkovModel) {
        return GenerateMarkovResponse(history);
    }
    if (gActiveAIModel == kOpenAIModel) {
        return GenerateOpenAIResponse(history);
    }

    /* Default case to avoid missing return */
    return "Error: Unknown model type";
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