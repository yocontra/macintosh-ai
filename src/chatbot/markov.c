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

/* Markov chain data structure */
#define MAX_WORDS 512
#define MAX_FOLLOWERS 16
#define MAX_WORD_LENGTH 32

typedef struct {
    char word[MAX_WORD_LENGTH];
    char followers[MAX_FOLLOWERS][MAX_WORD_LENGTH];
    short followerCount;
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

/* Find a word in the Markov chain, returns index or -1 if not found */
static short FindWordInChain(const char *word)
{
    short i;
    for (i = 0; i < gMarkovNodeCount; i++) {
        if (strcmp(gMarkovChain[i].word, word) == 0) {
            return i;
        }
    }
    return -1;
}

/* Add a word to the Markov chain, returns index */
static short AddWordToChain(const char *word)
{
    if (gMarkovNodeCount < MAX_WORDS) {
        strncpy(gMarkovChain[gMarkovNodeCount].word, word, MAX_WORD_LENGTH - 1);
        gMarkovChain[gMarkovNodeCount].word[MAX_WORD_LENGTH - 1] = '\0';
        gMarkovChain[gMarkovNodeCount].followerCount             = 0;
        return gMarkovNodeCount++;
    }
    return -1; /* Chain is full */
}

/* Add a follower to a word in the chain */
static void AddFollower(short wordIndex, const char *follower)
{
    MarkovNode *node = &gMarkovChain[wordIndex];

    /* Check if we already have this follower */
    short i;
    for (i = 0; i < node->followerCount; i++) {
        if (strcmp(node->followers[i], follower) == 0) {
            return; /* Already have this follower */
        }
    }

    /* Add new follower if there's space */
    if (node->followerCount < MAX_FOLLOWERS) {
        strncpy(node->followers[node->followerCount], follower, MAX_WORD_LENGTH - 1);
        node->followers[node->followerCount][MAX_WORD_LENGTH - 1] = '\0';
        node->followerCount++;
    }
}

/* Train the Markov chain with new text */
void TrainMarkov(const char *text)
{
    char buffer[kMaxPromptLength];
    char *word, *next_word;
    short wordIndex;

    /* Make a copy we can modify */
    strncpy(buffer, text, kMaxPromptLength - 1);
    buffer[kMaxPromptLength - 1] = '\0';

    /* Get first word */
    word = strtok(buffer, " \r\n\t.,!?;:");
    if (!word)
        return;

    /* Find or add the word to the chain */
    wordIndex = FindWordInChain(word);
    if (wordIndex < 0) {
        wordIndex = AddWordToChain(word);
        if (wordIndex < 0)
            return; /* Chain is full */
    }

    /* Process all words and their followers */
    while ((next_word = strtok(NULL, " \r\n\t.,!?;:")) != NULL) {
        /* Add the next word as a follower to the current word */
        AddFollower(wordIndex, next_word);

        /* Find or add the next word to the chain */
        short nextIndex = FindWordInChain(next_word);
        if (nextIndex < 0) {
            nextIndex = AddWordToChain(next_word);
            if (nextIndex < 0)
                break; /* Chain is full */
        }

        /* Move to the next word */
        wordIndex = nextIndex;
        word      = next_word;
    }
}

/* Initialize the Markov chain with data from markov_data.c */
static void InitMarkovChain(void)
{
    gMarkovNodeCount = 0;

    /* Training data is provided in markov_data.c */
    LoadTrainingData();
}

/* Generate a Markov chain response text */
static void GenerateMarkovText(char *response, short maxLength)
{
    char current_word[MAX_WORD_LENGTH];
    short i, wordIndex, followerIndex;
    short sentenceCount  = 0;
    short sentenceTarget = (RandomGen() % 3) + 1; /* 1-3 sentences */

    /* Start with a random word from the chain */
    if (gMarkovNodeCount == 0) {
        strcpy(response, "I don't have enough information yet.");
        return;
    }

    /* Clear the response */
    response[0] = '\0';

    /* Pick a starter word (preferably one that starts a sentence) */
    for (i = 0; i < 10; i++) { /* Try 10 times to find a good starter */
        wordIndex = RandomGen() % gMarkovNodeCount;
        if (gMarkovChain[wordIndex].word[0] >= 'A' && gMarkovChain[wordIndex].word[0] <= 'Z') {
            break;
        }
    }

    strcpy(current_word, gMarkovChain[wordIndex].word);
    strcat(response, current_word);

    /* Generate the response */
    while (strlen(response) < maxLength - MAX_WORD_LENGTH) {
        /* Try to find the current word in the chain */
        wordIndex = FindWordInChain(current_word);

        if (wordIndex < 0 || gMarkovChain[wordIndex].followerCount == 0) {
            /* Word not found or has no followers, pick a random word */
            wordIndex = RandomGen() % gMarkovNodeCount;
            strcat(response, ". "); /* End the sentence */
            sentenceCount++;

            if (sentenceCount >= sentenceTarget) {
                break; /* We've reached our target number of sentences */
            }
        }

        /* Select a random follower */
        followerIndex = RandomGen() % gMarkovChain[wordIndex].followerCount;
        strcpy(current_word, gMarkovChain[wordIndex].followers[followerIndex]);

        /* Add space before the next word */
        strcat(response, " ");
        strcat(response, current_word);

        /* Check for end of sentence markers */
        size_t len = strlen(current_word);
        if (len > 0 && (current_word[len - 1] == '.' || current_word[len - 1] == '!' ||
                        current_word[len - 1] == '?')) {
            sentenceCount++;
            if (sentenceCount >= sentenceTarget) {
                break; /* We've reached our target number of sentences */
            }
        }
    }

    /* Ensure the response ends with proper punctuation */
    size_t len = strlen(response);
    if (len > 0 && response[len - 1] != '.' && response[len - 1] != '!' &&
        response[len - 1] != '?') {
        strcat(response, ".");
    }
}

/* Initialize the Markov model */
void InitMarkovModel(void)
{
    /* Initialize random number generator */
    InitRandom();

    /* Initialize the Markov chain with some starter phrases */
    InitMarkovChain();
}

/* Function that returns an appropriate response based on user input using Markov model */
char *GenerateMarkovResponse(const ConversationHistory *history)
{
    static char response[512];
    DateTimeRec dateTime;

    /* Initialize with default response in case something goes wrong */
    strcpy(response, "Something went wrong. Try again?");

    if (history != NULL && history->count > 0) {
        /* Find the last user message */
        short i;
        for (i = history->count - 1; i >= 0; i--) {
            if (history->messages[i].type == kUserMessage) {
                const char *prompt = history->messages[i].text;
                GenerateMarkovText(response, 500);
            }
        }
    }

    return response;
}
