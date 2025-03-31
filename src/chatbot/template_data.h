#ifndef TEMPLATE_DATA_H
#define TEMPLATE_DATA_H

/* Maximum number of templates to store */
#define MAX_TEMPLATES 150

/* Maximum number of keywords to check */
#define MAX_KEYWORDS 40

/* Maximum number of patterns per template */
#define MAX_PATTERNS 5

/* Maximum length of a pattern or template text */
#define MAX_PATTERN_LENGTH 64
#define MAX_TEMPLATE_LENGTH 256

/* Structure for template system */
typedef struct {
    char patterns[MAX_PATTERNS][MAX_PATTERN_LENGTH]; /* Input patterns that trigger this template */
    char patternCount;                               /* Number of patterns for this template */
    char response[MAX_TEMPLATE_LENGTH];              /* Response template with slots */
    unsigned char category; /* Category of response (general, tech, etc.) */
} ResponseTemplate;

/* Category enum for organizing templates */
enum {
    kCategoryGeneral  = 0,
    kCategoryTech     = 1,
    kCategoryMac      = 2,
    kCategoryHelp     = 3,
    kCategoryGreeting = 4,
    kCategoryUnsure   = 5
};

/* Structure to store extracted keywords from user input */
typedef struct {
    char keyword[MAX_PATTERN_LENGTH];
    unsigned char importance; /* 0-255 score for keyword importance */
} ExtractedKeyword;

/* Initialize the template database with patterns and responses */
void LoadTemplateData(void);

#endif /* TEMPLATE_DATA_H */