#include <Memory.h>
#include <OSUtils.h>
#include <TextEdit.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../constants.h"
#include "markov.h"
#include "markov_data.h"

/* Markov chain data structure with bigram model (state_size=2) */
#define MAX_WORDS 384       /* Reduced to make room for more data per word */
#define MAX_FOLLOWERS 8     /* Reduced to make room for frequencies */
#define MAX_WORD_LENGTH 24  /* Reduced slightly to save memory */
#define MAX_STATE_LENGTH 48 /* For state (two words) */

/* New: Weighted followers to improve text quality */
typedef struct {
    char word[MAX_WORD_LENGTH];
    unsigned char frequency; /* Track how often this follower appears */
} WeightedFollower;

typedef struct {
    char state[MAX_STATE_LENGTH]; /* The state is now two words combined */
    WeightedFollower followers[MAX_FOLLOWERS];
    unsigned char followerCount;
    unsigned char isStartOfSentence : 1; /* Flag for sentence starters */
} MarkovNode;

/* Global Markov chain data */
static MarkovNode gMarkovChain[MAX_WORDS];
static short gMarkovNodeCount = 0;

/* Global random seed */
static unsigned long gRandomSeed = 1;

/* Custom random number generator - named differently to avoid conflicts */
static unsigned long RandomGen(void)
{
    gRandomSeed = gRandomSeed * 1103515245 + 12345;
    return (gRandomSeed >> 16) & 0x7FFF;
}

/* Initialize the random seed */
static void InitRandom(void)
{
    /* Use the system time to seed the random number generator */
    DateTimeRec dateTime;
    GetTime(&dateTime);

    /* Seed our custom random function with current time */
    gRandomSeed = (dateTime.hour * 3600) + (dateTime.minute * 60) + dateTime.second + dateTime.year;
}

/* Find a state in the Markov chain, returns index or -1 if not found */
static short FindStateInChain(const char *state)
{
    short i;
    for (i = 0; i < gMarkovNodeCount; i++) {
        if (strcmp(gMarkovChain[i].state, state) == 0) {
            return i;
        }
    }
    return -1;
}

/* Add a state to the Markov chain, returns index */
static short AddStateToChain(const char *state, Boolean isStart)
{
    if (gMarkovNodeCount < MAX_WORDS) {
        strncpy(gMarkovChain[gMarkovNodeCount].state, state, MAX_STATE_LENGTH - 1);
        gMarkovChain[gMarkovNodeCount].state[MAX_STATE_LENGTH - 1] = '\0';
        gMarkovChain[gMarkovNodeCount].followerCount               = 0;
        gMarkovChain[gMarkovNodeCount].isStartOfSentence           = isStart;
        return gMarkovNodeCount++;
    }
    return -1; /* Chain is full */
}

/* Add or update a follower to a state in the chain */
static void AddFollower(short stateIndex, const char *follower)
{
    MarkovNode *node = &gMarkovChain[stateIndex];
    short i;

    /* Check if we already have this follower */
    for (i = 0; i < node->followerCount; i++) {
        if (strcmp(node->followers[i].word, follower) == 0) {
            /* Found existing follower, increment frequency (up to 255) */
            if (node->followers[i].frequency < 255) {
                node->followers[i].frequency++;
            }
            return;
        }
    }

    /* Add new follower if there's space */
    if (node->followerCount < MAX_FOLLOWERS) {
        strncpy(node->followers[node->followerCount].word, follower, MAX_WORD_LENGTH - 1);
        node->followers[node->followerCount].word[MAX_WORD_LENGTH - 1] = '\0';
        node->followers[node->followerCount].frequency = 1; /* Initialize frequency */
        node->followerCount++;
    }
    else {
        /* Chain is full for this state, potentially replace a random low-frequency follower */
        short replace_idx = RandomGen() % MAX_FOLLOWERS;
        if (node->followers[replace_idx].frequency == 1) {
            strncpy(node->followers[replace_idx].word, follower, MAX_WORD_LENGTH - 1);
            node->followers[replace_idx].word[MAX_WORD_LENGTH - 1] = '\0';
            node->followers[replace_idx].frequency                 = 1;
        }
    }
}

/* Helper function to check if a char is sentence ending punctuation */
static Boolean IsSentenceEnder(char c)
{
    return (c == '.' || c == '!' || c == '?');
}

/* Helper function to combine two words into a state */
static void MakeState(const char *word1, const char *word2, char *state_buffer)
{
    strcpy(state_buffer, word1);
    strcat(state_buffer, " ");
    strcat(state_buffer, word2);
}

/* Train the Markov chain with new text using bigram model */
void TrainMarkov(const char *text)
{
    char buffer[kMaxPromptLength];
    char *word1, *word2, *word3;
    char state_buffer[MAX_STATE_LENGTH];
    short stateIndex;
    Boolean newSentence = TRUE;

    /* Make a copy we can modify */
    strncpy(buffer, text, kMaxPromptLength - 1);
    buffer[kMaxPromptLength - 1] = '\0';

    /* Get first word */
    word1 = strtok(buffer, " \r\n\t");
    if (!word1)
        return;

    /* Get second word */
    word2 = strtok(NULL, " \r\n\t");
    if (!word2)
        return;

    /* Remove punctuation from words except sentence enders */
    char *p           = word1 + strlen(word1) - 1;
    Boolean endsFirst = IsSentenceEnder(*p);
    while (p > word1 && strchr(".,!?;:", *p)) {
        if (!IsSentenceEnder(*p))
            *p = '\0';
        p--;
    }

    p = word2 + strlen(word2) - 1;
    while (p > word2 && strchr(".,!?;:", *p)) {
        if (!IsSentenceEnder(*p))
            *p = '\0';
        p--;
    }

    /* Create the first state (word1 + word2) */
    MakeState(word1, word2, state_buffer);

    /* Find or add the state to the chain, marking if it starts a sentence */
    stateIndex = FindStateInChain(state_buffer);
    if (stateIndex < 0) {
        stateIndex = AddStateToChain(state_buffer, newSentence);
        if (stateIndex < 0)
            return; /* Chain is full */
    }
    else if (newSentence) {
        /* Mark existing state as a sentence starter if we found it starting a sentence */
        gMarkovChain[stateIndex].isStartOfSentence = TRUE;
    }

    /* Process all words and add them to the chain */
    while ((word3 = strtok(NULL, " \r\n\t")) != NULL) {
        /* Check if previous word ended a sentence */
        if (endsFirst) {
            newSentence = TRUE;
            endsFirst   = FALSE;
        }
        else {
            size_t len = strlen(word2);
            if (len > 0 && IsSentenceEnder(word2[len - 1])) {
                newSentence = TRUE;
            }
            else {
                newSentence = FALSE;
            }
        }

        /* Clean punctuation from the new word except sentence enders */
        p = word3 + strlen(word3) - 1;
        while (p > word3 && strchr(".,!?;:", *p)) {
            if (!IsSentenceEnder(*p))
                *p = '\0';
            p--;
        }

        /* Add word3 as a follower to the current state */
        AddFollower(stateIndex, word3);

        /* Create new state (word2 + word3) */
        MakeState(word2, word3, state_buffer);

        /* Find or add the new state to the chain */
        short newStateIndex = FindStateInChain(state_buffer);
        if (newStateIndex < 0) {
            newStateIndex = AddStateToChain(state_buffer, newSentence);
            if (newStateIndex < 0)
                break; /* Chain is full */
        }
        else if (newSentence) {
            /* Mark existing state as a sentence starter if we found it starting a sentence */
            gMarkovChain[newStateIndex].isStartOfSentence = TRUE;
        }

        /* Move to the next state */
        stateIndex = newStateIndex;
        word1      = word2;
        word2      = word3;
    }
}

/* Initialize the Markov chain with data from markov_data.c */
static void InitMarkovChain(void)
{
    gMarkovNodeCount = 0;

    /* Training data is provided in markov_data.c */
    LoadTrainingData();
}

/* Select a follower based on weighted probabilities */
static short SelectWeightedFollower(const MarkovNode *node)
{
    if (node->followerCount == 0)
        return -1;

    /* Calculate total frequency */
    unsigned short totalWeight = 0;
    short i;

    for (i = 0; i < node->followerCount; i++) {
        totalWeight += node->followers[i].frequency;
    }

    /* Select randomly based on frequency weight */
    unsigned short randomVal     = RandomGen() % totalWeight;
    unsigned short currentWeight = 0;

    for (i = 0; i < node->followerCount; i++) {
        currentWeight += node->followers[i].frequency;
        if (randomVal < currentWeight) {
            return i;
        }
    }

    /* Fallback (shouldn't happen) */
    return RandomGen() % node->followerCount;
}

/* Extract second word from a state */
static void GetSecondWord(const char *state, char *word)
{
    const char *space = strchr(state, ' ');
    if (space) {
        strcpy(word, space + 1);
    }
    else {
        /* Fallback if no space found */
        strcpy(word, state);
    }
}

/* Generate a Markov chain response text */
static void GenerateMarkovText(char *response, short maxLength)
{
    char current_state[MAX_STATE_LENGTH];
    char next_word[MAX_WORD_LENGTH];
    char next_state[MAX_STATE_LENGTH];
    short stateIndex, followerIndex;
    short wordCount      = 0;
    short sentenceCount  = 0;
    short sentenceTarget = (RandomGen() % 2) + 1; /* 1-2 sentences */

    /* Start with a random state from the chain */
    if (gMarkovNodeCount == 0) {
        strcpy(response, "I don't have enough information yet.");
        return;
    }

    /* Clear the response */
    response[0] = '\0';

    /* Pick a starter state (preferably one that starts a sentence) */
    short attempts = 0;
    do {
        stateIndex = RandomGen() % gMarkovNodeCount;
        attempts++;
    } while (!gMarkovChain[stateIndex].isStartOfSentence && attempts < 20);

    /* Add the starting state to the response */
    strcpy(current_state, gMarkovChain[stateIndex].state);
    strcat(response, current_state);
    wordCount += 2; /* State has two words */

    /* Generate the response */
    while (strlen(response) < maxLength - MAX_WORD_LENGTH && sentenceCount < sentenceTarget) {
        /* Try to find the current state in the chain */
        stateIndex = FindStateInChain(current_state);

        if (stateIndex < 0 || gMarkovChain[stateIndex].followerCount == 0) {
            /* State not found or has no followers */
            strcat(response, ". "); /* End the sentence */
            sentenceCount++;

            /* Pick a new sentence starter state */
            do {
                stateIndex = RandomGen() % gMarkovNodeCount;
            } while (!gMarkovChain[stateIndex].isStartOfSentence && attempts < 10);

            strcpy(current_state, gMarkovChain[stateIndex].state);
            strcat(response, current_state);
            wordCount += 2;
        }
        else {
            /* Select a follower using weighted selection */
            followerIndex = SelectWeightedFollower(&gMarkovChain[stateIndex]);
            if (followerIndex < 0)
                continue;

            strcpy(next_word, gMarkovChain[stateIndex].followers[followerIndex].word);

            /* Add space and the next word */
            strcat(response, " ");
            strcat(response, next_word);
            wordCount++;

            /* Create the next state */
            char second_word[MAX_WORD_LENGTH];
            GetSecondWord(current_state, second_word);
            MakeState(second_word, next_word, next_state);
            strcpy(current_state, next_state);

            /* Check for end of sentence */
            size_t len = strlen(next_word);
            if (len > 0 && IsSentenceEnder(next_word[len - 1])) {
                sentenceCount++;
                if (sentenceCount < sentenceTarget) {
                    strcat(response, " ");
                }
            }
        }

        /* Avoid exceptionally long sentences */
        if (wordCount > 20 && sentenceCount < sentenceTarget) {
            short i = strlen(response) - 1;
            while (i > 0 && !strchr(".!?", response[i]))
                i--;

            if (i < strlen(response) - 15) { /* If no recent sentence ending */
                strcat(response, ". ");
                sentenceCount++;

                /* Pick a new sentence starter state */
                do {
                    stateIndex = RandomGen() % gMarkovNodeCount;
                } while (!gMarkovChain[stateIndex].isStartOfSentence && attempts < 10);

                strcpy(current_state, gMarkovChain[stateIndex].state);
                strcat(response, current_state);
                wordCount += 2;
            }
        }
    }

    /* Ensure the response ends with proper punctuation */
    size_t len = strlen(response);
    if (len > 0 && !strchr(".!?", response[len - 1])) {
        strcat(response, ".");
    }
}

/* Initialize the Markov model */
void InitMarkovModel(void)
{
    /* Initialize random number generator */
    InitRandom();

    /* Initialize the Markov chain with training data */
    InitMarkovChain();
}

/* Function that returns an appropriate response based on user input using Markov model */
char *GenerateMarkovResponse(const ConversationHistory *history)
{
    static char response[512];

    /* Initialize with default response in case something goes wrong */
    strcpy(response, "I'm thinking about how to respond...");

    if (history != NULL && history->count > 0) {
        /* Find the last user message */
        short i;
        for (i = history->count - 1; i >= 0; i--) {
            if (history->messages[i].type == kUserMessage) {
                /* Generate response based on markov chain */
                GenerateMarkovText(response, 500);
                break;
            }
        }
    }
    else {
        /* No history, generate response from general training */
        GenerateMarkovText(response, 500);
    }

    return response;
}
