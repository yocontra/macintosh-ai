#ifndef OPENAI_H
#define OPENAI_H

#include "markov.h"

void InitOpenAI(void);
char *GenerateOpenAIResponse(const ConversationHistory *history);

#endif /* OPENAI_H */