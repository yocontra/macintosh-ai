#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <Controls.h>
#include <Lists.h>
#include <Quickdraw.h>
#include <TextEdit.h>
#include <Windows.h>

/*********************************************************************
 * CONSTANTS
 *********************************************************************/

/* Resource IDs */
enum {
    /* Menu IDs */
    kMenuApple  = 128,
    kMenuFile   = 129,
    kMenuModels = 130,

    /* Menu item indices */
    kItemAbout = 1,
    kItemChat  = 1,
    kItemClose = 2,
    kItemQuit  = 4,

    /* Models menu items */
    kItemMarkovModel   = 1,
    kItemOpenAIModel   = 2,
    kItemTemplateModel = 3,

    /* Window resource IDs */
    kAboutBoxID   = 128,
    kMainWindowID = 129,
    kChatWindowID = 130,

    /* Button resource ID and value */
    kStartChatBtn = 128
};

/* Application modes */
enum {
    kModeMainSplash = 0, /* Main splash screen is shown */
    kModeChatWindow = 1  /* Chat window is active */
};

/* Control constants */
#define pushButProc 0
#define scrollBarProc 16

/* Control part codes (Toolbox constants that may not be in headers) */
#ifndef inUpButton
#define inUpButton 20
#define inDownButton 21
#define inPageUp 22
#define inPageDown 23
#define inThumb 129
#endif

/* Font identifiers */
enum { kFontSystem = 0, kFontGeneva = 3, kFontMonaco = 4 };

/* Error codes */
enum {
    kErrNoError          = 0,
    kErrMemoryFull       = 1,
    kErrWindowCreation   = 2,
    kErrControlCreation  = 3,
    kErrTextEditCreation = 4,
    kErrResourceNotFound = 5,
    kErrDiskFull         = 6,
    kErrUnknown          = 99
};

/* Error context codes - more efficient than passing strings */
enum {
    kCtxLaunchingApp         = 1,
    kCtxCreatingMainWindow   = 2,
    kCtxCreatingButton       = 3,
    kCtxShowingAboutBox      = 4,
    kCtxAboutBoxText         = 5,
    kCtxOpeningChatWindow    = 6,
    kCtxCreatingResponseArea = 7,
    kCtxGeneratingResponse   = 8,
    kCtxCreatingPromptArea   = 9,
    kCtxQuittingApplication  = 11
};

/* Color constants if not defined */
#ifndef blackColor
#define blackColor 33
#endif

#ifndef whiteColor
#define whiteColor 30
#endif

#ifndef blueColor
#define blueColor 204
#endif

/* Chat UI constants */
enum {
    kMaxPromptLength   = 256,
    kMaxResponseLength = 4096,
    kMaxResponses      = 20, /* Maximum number of responses in a conversation */
    kPromptMargin      = 10,
    kResponseMargin    = 10,
    kChatBoxPadding    = 5,
    kChatInputHeight   = 40,
    kChatBoxMaxWidth   = 400
};

/*********************************************************************
 * GLOBAL VARIABLES
 *********************************************************************/

/* External declarations for global variables defined in main.c */
extern short gAppMode; /* Current application mode */


#endif /* CONSTANTS_H */