#include <Devices.h>
#include <Files.h>
#include <Fonts.h>
#include <Gestalt.h>
#include <Memory.h>
#include <OSUtils.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../constants.h"
#include "template.h"
#include "template_data.h"

/* Global template database */
static ResponseTemplate gTemplates[MAX_TEMPLATES];
static short gTemplateCount = 0;

/* Global random seed */
static unsigned long gRandomSeed = 1;

/* Custom random number generator - named differently to avoid conflicts */
static unsigned long TemplateRandomGen(void)
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

/* Add a template with its patterns */
void AddTemplate(const char *response, unsigned char category, const char **patterns,
                 short patternCount)
{
    short i;

    /* Ensure we haven't exceeded the template limit */
    if (gTemplateCount >= MAX_TEMPLATES)
        return;

    /* Add the template */
    strncpy(gTemplates[gTemplateCount].response, response, MAX_TEMPLATE_LENGTH - 1);
    gTemplates[gTemplateCount].response[MAX_TEMPLATE_LENGTH - 1] = '\0';
    gTemplates[gTemplateCount].category                          = category;

    /* Add the patterns (limited by MAX_PATTERNS) */
    gTemplates[gTemplateCount].patternCount = 0;
    for (i = 0; i < patternCount && i < MAX_PATTERNS; i++) {
        strncpy(gTemplates[gTemplateCount].patterns[i], patterns[i], MAX_PATTERN_LENGTH - 1);
        gTemplates[gTemplateCount].patterns[i][MAX_PATTERN_LENGTH - 1] = '\0';
        gTemplates[gTemplateCount].patternCount++;
    }

    gTemplateCount++;
}

/* Convert string to lowercase for case-insensitive matching */
static void ConvertToLowercase(char *str)
{
    while (*str) {
        if (*str >= 'A' && *str <= 'Z')
            *str = *str + 32;
        str++;
    }
}

/* Add dynamic system information templates */
void AddDynamicSystemTemplates(void)
{
    const char *patterns[MAX_PATTERNS];
    DateTimeRec dateTime;
    char buffer[MAX_TEMPLATE_LENGTH];
    const char *monthNames[] = {"January",   "February", "March",    "April",
                                "May",       "June",     "July",     "August",
                                "September", "October",  "November", "December"};

    /* Initialize date/time patterns */
    patterns[0] = "current date";
    patterns[1] = "today date";
    patterns[2] = "what day";
    patterns[3] = "what date";

    /* Date is handled by template system with {{date}} slot */
    AddTemplate("Today is {{date}}.", kCategoryGeneral, patterns, 4);

    patterns[0] = "current time";
    patterns[1] = "what time";
    patterns[2] = "time now";

    /* Time is handled by template system with {{time}} slot */
    AddTemplate("The current time is {{time}}.", kCategoryGeneral, patterns, 3);

    /* Memory information */
    patterns[0] = "ram installed";
    patterns[1] = "memory installed";
    patterns[2] = "installed ram";
    patterns[3] = "total memory";

    long totalMem = MaxMem(NULL);
    if (totalMem > 0) {
        sprintf(buffer, "Your Mac has about %.1f MB of RAM installed.",
                (float)totalMem / (1024 * 1024));
        AddTemplate(buffer, kCategoryMac, patterns, 4);
    }

    patterns[0] = "free memory";
    patterns[1] = "available memory";
    patterns[2] = "free ram";

    long freeMem = FreeMem();
    if (freeMem > 0) {
        sprintf(buffer, "You have around %.1f MB of free memory available right now.",
                (float)freeMem / (1024 * 1024));
        AddTemplate(buffer, kCategoryMac, patterns, 3);
    }

    /* System version */
    patterns[0] = "system version";
    patterns[1] = "os version";
    patterns[2] = "which system";

    long sysVersion;
    if (Gestalt(gestaltSystemVersion, &sysVersion) == noErr) {
        short majorVersion = (sysVersion >> 8) & 0xFF;
        short minorVersion = sysVersion & 0xFF;
        sprintf(buffer, "You're running System %d.%d on your Mac.", majorVersion, minorVersion);
        AddTemplate(buffer, kCategoryMac, patterns, 3);
    }

    /* CPU type */
    patterns[0] = "processor";
    patterns[1] = "cpu";
    patterns[2] = "chip";

    long cpuType;
    if (Gestalt(gestaltProcessorType, &cpuType) == noErr) {
        const char *cpuName = "unknown";
        switch (cpuType) {
        case gestalt68000:
            cpuName = "Motorola 68000";
            break;
        case gestalt68010:
            cpuName = "Motorola 68010";
            break;
        case gestalt68020:
            cpuName = "Motorola 68020";
            break;
        case gestalt68030:
            cpuName = "Motorola 68030";
            break;
        case gestalt68040:
            cpuName = "Motorola 68040";
            break;
        }
        sprintf(buffer, "Your Mac has a %s processor.", cpuName);
        AddTemplate(buffer, kCategoryMac, patterns, 3);
    }

    /* System uptime */
    patterns[0] = "uptime";
    patterns[1] = "how long running";
    patterns[2] = "running time";

    unsigned long ticks   = TickCount();
    unsigned long seconds = ticks / 60; /* 60 ticks per second */
    unsigned long minutes = seconds / 60;
    unsigned long hours   = minutes / 60;
    minutes %= 60;

    if (hours > 0) {
        sprintf(buffer, "Your Mac has been running for %lu hours and %lu minutes.", hours, minutes);
    }
    else {
        sprintf(buffer, "Your Mac has been running for %lu minutes.", minutes);
    }
    AddTemplate(buffer, kCategoryMac, patterns, 3);
}

/* Check if a pattern exists in the text (case insensitive) */
static Boolean PatternMatches(const char *pattern, const char *text)
{
    char patternLower[MAX_PATTERN_LENGTH];
    char textLower[kMaxPromptLength];

    /* Make lowercase copies for case-insensitive matching */
    strncpy(patternLower, pattern, MAX_PATTERN_LENGTH - 1);
    patternLower[MAX_PATTERN_LENGTH - 1] = '\0';
    ConvertToLowercase(patternLower);

    strncpy(textLower, text, kMaxPromptLength - 1);
    textLower[kMaxPromptLength - 1] = '\0';
    ConvertToLowercase(textLower);

    /* Check if pattern exists in text */
    return (strstr(textLower, patternLower) != NULL);
}

/* Extract keywords from user input for more contextual responses */
static void ExtractKeywords(const char *input, ExtractedKeyword *keywords, short *keywordCount)
{
    char buffer[kMaxPromptLength];
    char *token;
    short count = 0;

    /* Skip empty inputs */
    if (!input || !*input) {
        *keywordCount = 0;
        return;
    }

    /* Copy input to buffer we can modify */
    strncpy(buffer, input, kMaxPromptLength - 1);
    buffer[kMaxPromptLength - 1] = '\0';

    /* Tokenize the input */
    token = strtok(buffer, " ,.!?;:\n\r\t");
    while (token && count < MAX_KEYWORDS) {
        /* Skip very short words and common words */
        if (strlen(token) >= 3 && strcmp(token, "the") != 0 && strcmp(token, "and") != 0 &&
            strcmp(token, "for") != 0 && strcmp(token, "that") != 0 && strcmp(token, "with") != 0) {

            /* Copy token to keyword array */
            strncpy(keywords[count].keyword, token, MAX_PATTERN_LENGTH - 1);
            keywords[count].keyword[MAX_PATTERN_LENGTH - 1] = '\0';
            ConvertToLowercase(keywords[count].keyword);

            /* Assign importance based on length and other factors */
            keywords[count].importance = 50 + (strlen(token) * 5);

            /* Increase importance for technical and specific terms */
            if (strstr("mac|macintosh|system|file|disk|memory|error|help|app|window|program|"
                       "software|problem|computer|network",
                       token)) {
                keywords[count].importance += 50;
            }

            count++;
        }
        token = strtok(NULL, " ,.!?;:\n\r\t");
    }

    *keywordCount = count;
}

/* Find the best template based on user input */
static short FindBestTemplate(const char *userInput, const ExtractedKeyword *keywords,
                              short keywordCount)
{
    short i, j, k;
    short bestIndex          = -1;
    unsigned short bestScore = 0;
    unsigned short currentScore;

    /* First try to match based on patterns */
    for (i = 0; i < gTemplateCount; i++) {
        currentScore = 0;

        /* Check each pattern for this template */
        for (j = 0; j < gTemplates[i].patternCount; j++) {
            if (PatternMatches(gTemplates[i].patterns[j], userInput)) {
                /* Pattern matched - add score based on pattern length */
                currentScore += 100 + strlen(gTemplates[i].patterns[j]);
            }
        }

        /* Check keyword matches */
        for (j = 0; j < keywordCount; j++) {
            for (k = 0; k < gTemplates[i].patternCount; k++) {
                if (strstr(gTemplates[i].patterns[k], keywords[j].keyword)) {
                    currentScore += keywords[j].importance;
                }
            }
        }

        /* Keep track of the best match */
        if (currentScore > bestScore) {
            bestScore = currentScore;
            bestIndex = i;
        }
    }

    /* If no good match found, fall back to a general template based on category */
    if (bestScore < 50) {
        /* Collect all general templates */
        short generalTemplates[MAX_TEMPLATES];
        short generalCount = 0;

        for (i = 0; i < gTemplateCount; i++) {
            if (gTemplates[i].category == kCategoryGeneral ||
                gTemplates[i].category == kCategoryUnsure) {
                generalTemplates[generalCount++] = i;
            }
        }

        /* Pick a random general template if available */
        if (generalCount > 0) {
            bestIndex = generalTemplates[TemplateRandomGen() % generalCount];
        }
        else {
            /* Absolute fallback - first template */
            bestIndex = 0;
        }
    }

    return bestIndex;
}

/* Fill template slots with keywords from user input */
static void FillTemplate(char *response, const char *templateText, const ExtractedKeyword *keywords,
                         short keywordCount)
{
    const char *src = templateText;
    char *dst       = response;
    short keywordIndex;

    while (*src && (dst - response) < kMaxPromptLength - 1) {
        if (*src == '{' && *(src + 1) == '{') {
            /* Found a template slot */
            src += 2; /* Skip {{ */

            if (strncmp(src, "keyword", 7) == 0) {
                /* Keyword slot */
                src += 7; /* Skip "keyword" */

                /* Extract index number if present */
                keywordIndex = 0;
                if (*src >= '0' && *src <= '9') {
                    keywordIndex = *src - '0';
                    src++;
                }

                /* Skip to end of slot marker */
                while (*src && *src != '}')
                    src++;
                if (*src == '}')
                    src++; /* Skip first } */
                if (*src == '}')
                    src++; /* Skip second } */

                /* Insert the keyword if available */
                if (keywordIndex < keywordCount) {
                    strcpy(dst, keywords[keywordIndex].keyword);
                    dst += strlen(keywords[keywordIndex].keyword);
                }
                else {
                    /* No keyword available, insert placeholder */
                    strcpy(dst, "that");
                    dst += 4;
                }
            }
            else if (strncmp(src, "time", 4) == 0) {
                /* Insert current time */
                DateTimeRec dateTime;
                char timeStr[20];

                GetTime(&dateTime);
                sprintf(timeStr, "%d:%02d", dateTime.hour, dateTime.minute);

                strcpy(dst, timeStr);
                dst += strlen(timeStr);

                /* Skip to end of slot marker */
                while (*src && *src != '}')
                    src++;
                if (*src == '}')
                    src++; /* Skip first } */
                if (*src == '}')
                    src++; /* Skip second } */
            }
            else if (strncmp(src, "date", 4) == 0) {
                /* Insert current date */
                DateTimeRec dateTime;
                char dateStr[30];
                const char *monthNames[] = {"January",   "February", "March",    "April",
                                            "May",       "June",     "July",     "August",
                                            "September", "October",  "November", "December"};

                GetTime(&dateTime);
                sprintf(dateStr, "%s %d, %d", monthNames[dateTime.month - 1], dateTime.day,
                        dateTime.year);

                strcpy(dst, dateStr);
                dst += strlen(dateStr);

                /* Skip to end of slot marker */
                while (*src && *src != '}')
                    src++;
                if (*src == '}')
                    src++; /* Skip first } */
                if (*src == '}')
                    src++; /* Skip second } */
            }
            else {
                /* Unknown slot type, just skip it */
                while (*src && *src != '}')
                    src++;
                if (*src == '}')
                    src++; /* Skip first } */
                if (*src == '}')
                    src++; /* Skip second } */
            }
        }
        else {
            /* Regular character, copy it */
            *dst++ = *src++;
        }
    }

    /* Ensure null termination */
    *dst = '\0';
}

/* Initialize the Template-based model */
void InitTemplateModel(void)
{
    /* Initialize random number generator */
    InitRandom();

    /* Reset template count */
    gTemplateCount = 0;

    /* Add dynamic system information templates */
    AddDynamicSystemTemplates();

    /* Load template database */
    LoadTemplateData();
}

/* Generate a template-based AI response */
char *GenerateTemplateResponse(const ConversationHistory *history)
{
    static char response[512];
    ExtractedKeyword keywords[MAX_KEYWORDS];
    short keywordCount = 0;
    short templateIndex;
    const char *userMessage = NULL;

    /* Initialize with default response in case something goes wrong */
    strcpy(response, "I'm thinking about how to respond...");

    /* Check history is valid and get last user message */
    if (history != NULL && history->count > 0) {
        short i;
        for (i = history->count - 1; i >= 0; i--) {
            short idx = (history->head + i) % kMaxConversationHistory;
            if (history->messages[idx].type == kUserMessage) {
                userMessage = history->messages[idx].text;
                break;
            }
        }

        if (userMessage && *userMessage) {
            /* Extract keywords from user input */
            ExtractKeywords(userMessage, keywords, &keywordCount);

            /* Find the best matching template */
            templateIndex = FindBestTemplate(userMessage, keywords, keywordCount);

            if (templateIndex >= 0) {
                /* Fill the template with keywords from user input */
                FillTemplate(response, gTemplates[templateIndex].response, keywords, keywordCount);
            }

            return response;
        }
    }

    /* Fallback response if no user message found */
    strcpy(response, "Hello! I'm your Macintosh AI assistant. How can I help you today?");
    return response;
}