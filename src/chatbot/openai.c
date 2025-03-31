#include <stdio.h>
#include <string.h>

#include "../constants.h"
#include "markov.h"

/* Initialize the OpenAI connection */
void InitOpenAI(void)
{
    // TODO
}

/* Function that returns a remote AI response */
char *GenerateOpenAIResponse(const ConversationHistory *history)
{
    static char response[512];

    // TODO: Stub implementation - this will be replaced with real API calls later
    strcpy(response, "Remote AI response coming soon.");

    return response;
}