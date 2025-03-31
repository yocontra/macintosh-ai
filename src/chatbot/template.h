#ifndef TEMPLATE_H
#define TEMPLATE_H

#include "markov.h"

/* Initialize the Template-based model */
void InitTemplateModel(void);

/* Add dynamic system information templates */
void AddDynamicSystemTemplates(void);

/* Generate a template-based AI response */
char *GenerateTemplateResponse(const ConversationHistory *history);

#endif /* TEMPLATE_H */