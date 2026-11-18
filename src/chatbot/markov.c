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
/* The static corpus in markov_data.c yields 5442 distinct bigram states
   (measured with a host program); the dynamic system-data sentences add
   fewer than 100 more. MAX_WORDS leaves headroom so training never stops. */
#define MAX_WORDS 5632      /* 5632 * 144 bytes = 792 KB static */
#define MAX_FOLLOWERS 4     /* Corpus averages ~1.9 followers per state */
#define MAX_WORD_LENGTH 24  /* Longest corpus word is 23 characters + NUL */
#define MAX_STATE_LENGTH 40 /* Longest corpus state is 33 characters + NUL */

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
    short nextInBucket;                  /* Next state in the same hash bucket, -1 ends */
} MarkovNode;

/* Bucket heads for the state hash table. Training the full corpus is only
   affordable with hashed lookup; the bucket chain keeps chains ~5 entries. */
#define HASH_BUCKETS 1024

/* Global Markov chain data */
static MarkovNode gMarkovChain[MAX_WORDS];
static short gHashBuckets[HASH_BUCKETS];
static short gMarkovNodeCount = 0;

/* Global random seed */
static unsigned long gRandomSeed = 1;

/* Copy src into dst, never writing past dst[cap - 1]. Always NUL terminates. */
static void CopyBounded(char *dst, short cap, const char *src)
{
    short i = 0;

    if (cap <= 0)
        return;

    while (src[i] != '\0' && i < cap - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Append src to the NUL-terminated dst, never writing past dst[cap - 1].
   Returns the resulting length so callers can track the remaining space. */
static short AppendBounded(char *dst, short cap, const char *src)
{
    short len = (short)strlen(dst);
    short i   = 0;

    if (cap <= 0)
        return 0;

    if (len > cap - 1)
        len = cap - 1;

    while (src[i] != '\0' && len < cap - 1) {
        dst[len++] = src[i++];
    }
    dst[len] = '\0';
    return len;
}

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

/* Hash function for state lookup */
static unsigned short HashString(const char *state)
{
    unsigned short hash = 0;
    while (*state) {
        hash = (hash * 31) + *state++;
    }
    return hash;
}

/* Find a state in the Markov chain, returns index or -1 if not found */
static short FindStateInChain(const char *state)
{
    short i;

    if (gMarkovNodeCount <= 0)
        return -1;

    for (i = gHashBuckets[HashString(state) % HASH_BUCKETS]; i >= 0;
         i = gMarkovChain[i].nextInBucket) {
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
        unsigned short bucket = HashString(state) % HASH_BUCKETS;
        short index           = gMarkovNodeCount;

        strncpy(gMarkovChain[index].state, state, MAX_STATE_LENGTH - 1);
        gMarkovChain[index].state[MAX_STATE_LENGTH - 1] = '\0';
        gMarkovChain[index].followerCount               = 0;
        gMarkovChain[index].isStartOfSentence           = isStart;
        gMarkovChain[index].nextInBucket                = gHashBuckets[bucket];
        gHashBuckets[bucket]                            = index;

        gMarkovNodeCount++;
        return index;
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
static void MakeState(const char *word1, const char *word2, char *state_buffer, short destSize)
{
    CopyBounded(state_buffer, destSize, word1);
    AppendBounded(state_buffer, destSize, " ");
    AppendBounded(state_buffer, destSize, word2);
}

/* Clean and check if word ends a sentence */
static Boolean CleanWord(char *word, Boolean *isEndOfSentence)
{
    char *p;
    size_t len;

    if (!word || !*word)
        return FALSE;

    len = strlen(word);
    if (len == 0)
        return FALSE;

    /* Check for sentence ending punctuation */
    p                = word + len - 1;
    *isEndOfSentence = IsSentenceEnder(*p);

    /* Remove punctuation except sentence enders */
    while (p > word && strchr(".,!?;:", *p)) {
        if (!IsSentenceEnder(*p))
            *p = '\0';
        p--;
    }

    return TRUE;
}

/* Train the Markov chain with new text using bigram model */
void TrainMarkov(const char *text)
{
    char buffer[kMaxPromptLength];
    char *word1, *word2, *word3;
    char state_buffer[MAX_STATE_LENGTH];
    short stateIndex, newStateIndex;
    Boolean newSentence = TRUE;
    Boolean endsFirst   = FALSE;

    if (!text || !*text)
        return;

    /* Make a copy we can modify */
    strncpy(buffer, text, kMaxPromptLength - 1);
    buffer[kMaxPromptLength - 1] = '\0';

    /* Get first word */
    word1 = strtok(buffer, " \r\n\t");
    if (!CleanWord(word1, &endsFirst))
        return;

    /* Get second word */
    word2 = strtok(NULL, " \r\n\t");
    if (!CleanWord(word2, &endsFirst))
        return;

    /* Create the first state (word1 + word2) and find or add it */
    MakeState(word1, word2, state_buffer, sizeof(state_buffer));
    stateIndex = FindStateInChain(state_buffer);

    if (stateIndex < 0) {
        stateIndex = AddStateToChain(state_buffer, newSentence);
        if (stateIndex < 0)
            return; /* Chain is full */
    }
    else if (newSentence) {
        /* Mark existing state as a sentence starter */
        gMarkovChain[stateIndex].isStartOfSentence = TRUE;
    }

    /* Process all words and add them to the chain */
    while ((word3 = strtok(NULL, " \r\n\t")) != NULL) {
        Boolean wordEndsWithPunctuation;

        /* Check if previous word ended a sentence */
        newSentence = endsFirst || (strlen(word2) > 0 && IsSentenceEnder(word2[strlen(word2) - 1]));

        /* Clean current word */
        if (!CleanWord(word3, &wordEndsWithPunctuation))
            continue;

        /* Add word3 as a follower to the current state */
        AddFollower(stateIndex, word3);

        /* Create and add the new state (word2 + word3) */
        MakeState(word2, word3, state_buffer, sizeof(state_buffer));
        newStateIndex = FindStateInChain(state_buffer);

        if (newStateIndex < 0) {
            newStateIndex = AddStateToChain(state_buffer, newSentence);
            if (newStateIndex < 0)
                break; /* Chain is full */
        }
        else if (newSentence) {
            /* Mark existing state as a sentence starter */
            gMarkovChain[newStateIndex].isStartOfSentence = TRUE;
        }

        /* Move to the next state */
        stateIndex = newStateIndex;
        word1      = word2;
        word2      = word3;
        endsFirst  = wordEndsWithPunctuation;
    }
}

/* Initialize the Markov chain with data from markov_data.c */
static void InitMarkovChain(void)
{
    short i;

    gMarkovNodeCount = 0;
    for (i = 0; i < HASH_BUCKETS; i++)
        gHashBuckets[i] = -1;

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
static void GetSecondWord(const char *state, char *word, short destSize)
{
    const char *space = strchr(state, ' ');

    if (space) {
        CopyBounded(word, destSize, space + 1);
    }
    else {
        /* Fallback if no space found */
        CopyBounded(word, destSize, state);
    }
}

/* Generate a Markov chain response text */
static void GenerateMarkovText(char *response, short maxLength)
{
    char current_state[MAX_STATE_LENGTH];
    char next_word[MAX_WORD_LENGTH];
    char next_state[MAX_STATE_LENGTH];
    char second_word[MAX_WORD_LENGTH];
    short stateIndex, followerIndex;
    short wordCount      = 0;
    short sentenceCount  = 0;
    short sentenceTarget = (RandomGen() % 2) + 1; /* 1-2 sentences */
    short cap;
    short attempts;

    /* Never write past the caller's buffer, whatever the requested length */
    if (maxLength <= 0)
        return;
    cap = (maxLength > kMaxResponseLength) ? (short)kMaxResponseLength : maxLength;

    /* Start with a random state from the chain */
    if (gMarkovNodeCount == 0) {
        CopyBounded(response, cap, "I don't have enough information yet.");
        return;
    }

    /* Clear the response */
    response[0] = '\0';

    /* Pick a starter state (preferably one that starts a sentence) */
    attempts = 0;
    do {
        stateIndex = RandomGen() % gMarkovNodeCount;
        attempts++;
    } while (!gMarkovChain[stateIndex].isStartOfSentence && attempts < 20);

    /* Add the starting state to the response */
    CopyBounded(current_state, sizeof(current_state), gMarkovChain[stateIndex].state);
    AppendBounded(response, cap, current_state);
    wordCount += 2; /* State has two words */

    /* Generate the response. Every append is bounded by cap, and the guard
       below leaves room for a whole state plus punctuation. */
    while ((short)strlen(response) + MAX_STATE_LENGTH + 2 < cap &&
           sentenceCount < sentenceTarget) {
        /* Try to find the current state in the chain */
        stateIndex = FindStateInChain(current_state);

        if (stateIndex < 0 || gMarkovChain[stateIndex].followerCount == 0) {
            /* State not found or has no followers */
            AppendBounded(response, cap, ". "); /* End the sentence */
            sentenceCount++;

            /* Pick a new sentence starter state */
            attempts = 0;
            do {
                stateIndex = RandomGen() % gMarkovNodeCount;
                attempts++;
            } while (!gMarkovChain[stateIndex].isStartOfSentence && attempts < 10);

            CopyBounded(current_state, sizeof(current_state), gMarkovChain[stateIndex].state);
            AppendBounded(response, cap, current_state);
            wordCount += 2;
        }
        else {
            /* Select a follower using weighted selection */
            followerIndex = SelectWeightedFollower(&gMarkovChain[stateIndex]);
            if (followerIndex < 0)
                continue;

            CopyBounded(next_word, sizeof(next_word),
                        gMarkovChain[stateIndex].followers[followerIndex].word);

            /* Add space and the next word */
            AppendBounded(response, cap, " ");
            AppendBounded(response, cap, next_word);
            wordCount++;

            /* Create the next state */
            GetSecondWord(current_state, second_word, sizeof(second_word));
            MakeState(second_word, next_word, next_state, sizeof(next_state));
            CopyBounded(current_state, sizeof(current_state), next_state);

            /* Check for end of sentence */
            size_t len = strlen(next_word);
            if (len > 0 && IsSentenceEnder(next_word[len - 1])) {
                sentenceCount++;
                if (sentenceCount < sentenceTarget) {
                    AppendBounded(response, cap, " ");
                }
            }
        }

        /* Avoid exceptionally long sentences */
        if (wordCount > 20 && sentenceCount < sentenceTarget) {
            short i        = (short)strlen(response) - 1;
            short respLen  = (short)strlen(response);

            while (i > 0 && !strchr(".!?", response[i]))
                i--;

            if (i < respLen - 15) { /* If no recent sentence ending */
                AppendBounded(response, cap, ". ");
                sentenceCount++;

                /* Pick a new sentence starter state */
                attempts = 0;
                do {
                    stateIndex = RandomGen() % gMarkovNodeCount;
                    attempts++;
                } while (!gMarkovChain[stateIndex].isStartOfSentence && attempts < 10);

                CopyBounded(current_state, sizeof(current_state), gMarkovChain[stateIndex].state);
                AppendBounded(response, cap, current_state);
                wordCount += 2;
            }
        }
    }

    /* Ensure the response ends with proper punctuation */
    size_t len = strlen(response);
    if (len > 0 && !strchr(".!?", response[len - 1])) {
        AppendBounded(response, cap, ".");
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

/* Check if a state contains a keyword */
static Boolean ContainsKeyword(const char *state, const char *keyword)
{
    char stateLower[MAX_STATE_LENGTH];
    char keywordLower[MAX_WORD_LENGTH];
    short i;

    /* Convert state to lowercase for case-insensitive matching.
       Both sources are truncated to the destination size, never overflowed. */
    for (i = 0; state[i] != '\0' && i < MAX_STATE_LENGTH - 1; i++) {
        stateLower[i] = (state[i] >= 'A' && state[i] <= 'Z') ? state[i] + 32 : state[i];
    }
    stateLower[i] = '\0';

    /* Convert keyword to lowercase */
    for (i = 0; keyword[i] != '\0' && i < MAX_WORD_LENGTH - 1; i++) {
        keywordLower[i] = (keyword[i] >= 'A' && keyword[i] <= 'Z') ? keyword[i] + 32 : keyword[i];
    }
    keywordLower[i] = '\0';

    /* Check if keyword is in state */
    return (strstr(stateLower, keywordLower) != NULL);
}

/* Find a good starting state based on user query keywords */
static short FindRelevantStartingState(const char *userMessage)
{
    short i, bestIndex = -1;
    short relevanceScore = 0;
    short bestScore      = 0;
    char *msgCopy, *token;
    const char *keywords[] = {"science",  "computer", "mac",     "help",  "what",
                              "how",      "why",      "health",  "time",  "digital",
                              "creative", "learn",    "think",   "music", "art",
                              "history",  "code",     "program", "system"};
    short keywordCount     = sizeof(keywords) / sizeof(keywords[0]);
    short attempts         = 0;

    /* If no user message or very short, just return a random state */
    if (!userMessage || strlen(userMessage) < 4) {
        goto use_random_state;
    }

    /* Make a copy of the user message that we can modify */
    {
        size_t msgLen = strlen(userMessage);
        msgCopy       = (char *)NewPtr(msgLen + 1);
        if (!msgCopy) {
            goto use_random_state;
        }
        memcpy(msgCopy, userMessage, msgLen + 1);
    }

    /* First check for exact matches with the predefined keywords */
    for (i = 0; i < keywordCount; i++) {
        if (strstr(msgCopy, keywords[i]) != NULL) {
            /* Search for states containing this keyword */
            short j;
            for (j = 0; j < gMarkovNodeCount; j++) {
                if (ContainsKeyword(gMarkovChain[j].state, keywords[i]) &&
                    gMarkovChain[j].isStartOfSentence) {
                    /* Found a relevant starter state */
                    bestIndex = j;
                    DisposePtr(msgCopy);
                    return bestIndex;
                }
            }
        }
    }

    /* If no match with predefined keywords, try to use the user's own words */
    token = strtok(msgCopy, " ,.!?");
    while (token != NULL) {
        /* Skip very short words and common words */
        if (strlen(token) >= 4 && strcmp(token, "this") != 0 && strcmp(token, "that") != 0 &&
            strcmp(token, "with") != 0 && strcmp(token, "from") != 0 &&
            strcmp(token, "what") != 0 && strcmp(token, "when") != 0 &&
            strcmp(token, "where") != 0 && strcmp(token, "which") != 0 &&
            strcmp(token, "have") != 0 && strcmp(token, "your") != 0) {

            /* Look for states containing this word */
            short j;
            for (j = 0; j < gMarkovNodeCount; j++) {
                if (ContainsKeyword(gMarkovChain[j].state, token)) {
                    relevanceScore = 1;
                    /* Prefer sentence starters with higher follower counts */
                    if (gMarkovChain[j].isStartOfSentence) {
                        relevanceScore += 2;
                    }
                    if (gMarkovChain[j].followerCount > 2) {
                        relevanceScore += 1;
                    }

                    if (relevanceScore > bestScore) {
                        bestScore = relevanceScore;
                        bestIndex = j;
                    }
                }
            }
        }
        token = strtok(NULL, " ,.!?");
    }

    DisposePtr(msgCopy);

    /* If we found a relevant state, use it */
    if (bestIndex >= 0) {
        return bestIndex;
    }

use_random_state:
    /* Fallback to random sentence starter state */
    do {
        bestIndex = RandomGen() % gMarkovNodeCount;
        attempts++;
    } while (!gMarkovChain[bestIndex].isStartOfSentence && attempts < 20);

    return bestIndex;
}

/* Generate a Markov chain response based on user input */
static void GenerateContextualMarkovText(char *response, short maxLength, const char *userMessage)
{
    char current_state[MAX_STATE_LENGTH];
    char next_word[MAX_WORD_LENGTH];
    char next_state[MAX_STATE_LENGTH];
    char second_word[MAX_WORD_LENGTH];
    short stateIndex, followerIndex;
    short wordCount      = 0;
    short sentenceCount  = 0;
    short sentenceTarget = (RandomGen() % 2) + 1; /* 1-2 sentences */
    short cap;
    short attempts;

    /* Never write past the caller's buffer, whatever the requested length */
    if (maxLength <= 0)
        return;
    cap = (maxLength > kMaxResponseLength) ? (short)kMaxResponseLength : maxLength;

    /* Start with a state related to the user query if possible */
    if (gMarkovNodeCount == 0) {
        CopyBounded(response, cap, "I don't have enough information yet.");
        return;
    }

    /* Clear the response */
    response[0] = '\0';

    /* Try to find a relevant starting state */
    stateIndex = FindRelevantStartingState(userMessage);

    /* Add the starting state to the response */
    CopyBounded(current_state, sizeof(current_state), gMarkovChain[stateIndex].state);
    AppendBounded(response, cap, current_state);
    wordCount += 2; /* State has two words */

    /* Generate the response - same as before */
    while ((short)strlen(response) + MAX_STATE_LENGTH + 2 < cap &&
           sentenceCount < sentenceTarget) {
        /* Try to find the current state in the chain */
        stateIndex = FindStateInChain(current_state);

        if (stateIndex < 0 || gMarkovChain[stateIndex].followerCount == 0) {
            /* State not found or has no followers */
            AppendBounded(response, cap, ". "); /* End the sentence */
            sentenceCount++;

            /* Pick a new sentence starter state */
            attempts = 0;
            do {
                stateIndex = RandomGen() % gMarkovNodeCount;
                attempts++;
            } while (!gMarkovChain[stateIndex].isStartOfSentence && attempts < 10);

            CopyBounded(current_state, sizeof(current_state), gMarkovChain[stateIndex].state);
            AppendBounded(response, cap, current_state);
            wordCount += 2;
        }
        else {
            /* Select a follower using weighted selection */
            followerIndex = SelectWeightedFollower(&gMarkovChain[stateIndex]);
            if (followerIndex < 0)
                continue;

            CopyBounded(next_word, sizeof(next_word),
                        gMarkovChain[stateIndex].followers[followerIndex].word);

            /* Add space and the next word */
            AppendBounded(response, cap, " ");
            AppendBounded(response, cap, next_word);
            wordCount++;

            /* Create the next state */
            GetSecondWord(current_state, second_word, sizeof(second_word));
            MakeState(second_word, next_word, next_state, sizeof(next_state));
            CopyBounded(current_state, sizeof(current_state), next_state);

            /* Check for end of sentence */
            size_t len = strlen(next_word);
            if (len > 0 && IsSentenceEnder(next_word[len - 1])) {
                sentenceCount++;
                if (sentenceCount < sentenceTarget) {
                    AppendBounded(response, cap, " ");
                }
            }
        }

        /* Avoid exceptionally long sentences */
        if (wordCount > 20 && sentenceCount < sentenceTarget) {
            short i       = (short)strlen(response) - 1;
            short respLen = (short)strlen(response);

            while (i > 0 && !strchr(".!?", response[i]))
                i--;

            if (i < respLen - 15) { /* If no recent sentence ending */
                AppendBounded(response, cap, ". ");
                sentenceCount++;

                /* Pick a new sentence starter state */
                attempts = 0;
                do {
                    stateIndex = RandomGen() % gMarkovNodeCount;
                    attempts++;
                } while (!gMarkovChain[stateIndex].isStartOfSentence && attempts < 10);

                CopyBounded(current_state, sizeof(current_state), gMarkovChain[stateIndex].state);
                AppendBounded(response, cap, current_state);
                wordCount += 2;
            }
        }
    }

    /* Ensure the response ends with proper punctuation */
    size_t len = strlen(response);
    if (len > 0 && !strchr(".!?", response[len - 1])) {
        AppendBounded(response, cap, ".");
    }
}

/* Function that returns an appropriate response based on user input using Markov model */
char *GenerateMarkovResponse(const ConversationHistory *history)
{
    static char response[kMaxResponseLength];
    const char *userMessage = NULL;

    /* Initialize with default response in case something goes wrong */
    CopyBounded(response, (short)sizeof(response), "I'm thinking about how to respond...");

    if (history != NULL && history->count > 0) {
        /* Find the last user message. head is the oldest entry, so index
           (head + i) % kMaxConversationHistory walks newest to oldest even
           after the circular buffer has wrapped. */
        short i;
        for (i = history->count - 1; i >= 0; i--) {
            short idx = (history->head + i) % kMaxConversationHistory;
            if (history->messages[idx].type == kUserMessage) {
                userMessage = history->messages[idx].text;
                break;
            }
        }

        if (userMessage && *userMessage) {
            /* Generate contextual response based on markov chain */
            GenerateContextualMarkovText(response, (short)sizeof(response), userMessage);
            return response;
        }
    }

    /* No history or no user message, generate generic response */
    GenerateMarkovText(response, (short)sizeof(response));
    return response;
}
