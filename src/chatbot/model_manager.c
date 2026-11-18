#include <Memory.h>
#include <stdio.h>
#include <string.h>

#include "../constants.h"
#include "markov.h"
#include "model_manager.h"
#include "template.h"

/* Global conversation history */
ConversationHistory gConversationHistory;

/* Default to Markov model */
AIModelType gActiveAIModel = kMarkovModel;

/* Track initialization status */
Boolean gModelsInitialized = false;

/* Initialize all AI models and conversation history */
void InitModels(void)
{
    const char *welcomeMsg = "AI initialized! How can I help you today?";

    /* Clear the conversation history and initialize circular buffer */
    gConversationHistory.count  = 0;
    gConversationHistory.head   = 0;
    gConversationHistory.isFull = 0;
    memset(gConversationHistory.messages, 0, sizeof(ConversationMessage) * kMaxConversationHistory);

    /* Initialize the selected model if not already initialized */
    if (!gModelsInitialized) {
        if (gActiveAIModel == kMarkovModel) {
            InitMarkovModel();
        }
        else if (gActiveAIModel == kTemplateModel) {
            InitTemplateModel();
        }

        gModelsInitialized = true;
    }

    /* Add welcome message to the conversation history */
    if (gActiveAIModel == kMarkovModel) {
        welcomeMsg = "AI initialized! Using Markov chain model. How can I help you today?";
    }
    else if (gActiveAIModel == kRemoteModel) {
        welcomeMsg = "AI initialized! Using remote AI. How can I help you today?";
    }
    else if (gActiveAIModel == kTemplateModel) {
        welcomeMsg = "AI initialized! Using Template-based model. How can I help you today?";
    }

    AddAIResponse(welcomeMsg);
}

/* Set the active AI model */
void SetActiveAIModel(AIModelType modelType)
{
    /* Only change model and initialize if it's a different model */
    if (gActiveAIModel != modelType) {
        gActiveAIModel = modelType;

        /* Initialize the newly selected model */
        if (modelType == kMarkovModel) {
            InitMarkovModel();
        }
        else if (modelType == kTemplateModel) {
            InitTemplateModel();
        }
    }
}

/* Generate AI response based on active model */
char *GenerateAIResponse(const ConversationHistory *history)
{
    if (gActiveAIModel == kMarkovModel) {
        return GenerateMarkovResponse(history);
    }
    if (gActiveAIModel == kTemplateModel) {
        return GenerateTemplateResponse(history);
    }

    /* The remote model is driven asynchronously by the API client in the
       chat window, so there is no synchronous response to generate here. */
    return "Error: Unknown model type";
}

/* Calculate the tail index (position for the new item) in the circular buffer.
   Invariant: head is the oldest live entry; when count < kMaxConversationHistory
   the newest entry is at head + count - 1, and when isFull is set count equals
   kMaxConversationHistory and the next write replaces the entry at head. */
static short CircularBufferTail(void)
{
    if (gConversationHistory.isFull) {
        /* If buffer is full, new tail is where the current head is (we'll advance head after) */
        return gConversationHistory.head;
    }
    else if (gConversationHistory.count == 0) {
        /* If buffer is empty, tail is same as head */
        return gConversationHistory.head;
    }
    else {
        /* Otherwise, tail is (head + count) % size */
        return (gConversationHistory.head + gConversationHistory.count) % kMaxConversationHistory;
    }
}

/* Add an item to the circular buffer and update head/count/isFull */
static void AddToCircularBuffer(MessageType type, const char *text)
{
    short tail = CircularBufferTail();

    /* Add the new message at the tail position.
       History keeps at most kMaxConversationText - 1 characters; the display
       buffer still shows the full text. */
    gConversationHistory.messages[tail].type = type;
    strncpy(gConversationHistory.messages[tail].text, text, kMaxConversationText - 1);
    gConversationHistory.messages[tail].text[kMaxConversationText - 1] = '\0';

    /* Update buffer state */
    if (gConversationHistory.isFull) {
        /* If buffer was full, advance head to overwrite oldest item */
        gConversationHistory.head = (gConversationHistory.head + 1) % kMaxConversationHistory;
    }
    else {
        /* Otherwise increment count and set isFull if we reached capacity */
        gConversationHistory.count++;
        if (gConversationHistory.count == kMaxConversationHistory) {
            gConversationHistory.isFull = 1;
        }
    }
}

/* Add a user prompt to the conversation */
void AddUserPrompt(const char *prompt)
{
    AddToCircularBuffer(kUserMessage, prompt);
}

/* Add an AI response to the conversation */
void AddAIResponse(const char *response)
{
    AddToCircularBuffer(kAIMessage, response);
}