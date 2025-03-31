#include "Types.r"
#include "Menus.r"
#include "Windows.r"
#include "Dialogs.r"

/* Apple menu */
resource 'MENU' (128) {
    128, textMenuProc;
    allEnabled, enabled;
    apple;
    {
        "About AI Assistant...", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
    }
};

/* File menu */
resource 'MENU' (129) {
    129, textMenuProc;
    allEnabled, enabled;
    "File";
    {
        "Chat", noIcon, "L", noMark, plain;
        "Close", noIcon, "W", noMark, plain;
        "Toggle AI Model", noIcon, "M", noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Quit", noIcon, "Q", noMark, plain;
    }
};

/* Edit menu */
resource 'MENU' (130) {
    130, textMenuProc;
    0, enabled;
    "Edit";
    {
        "Undo", noIcon, "Z", noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Cut", noIcon, "X", noMark, plain;
        "Copy", noIcon, "C", noMark, plain;
        "Paste", noIcon, "V", noMark, plain;
        "Clear", noIcon, noKey, noMark, plain;
    }
};

/* Menu bar definition */
resource 'MBAR' (128) {
    { 128, 129, 130 };
};

/* String resources */
resource 'STR#' (128) {
    {
        "AI Assistant";
    }
};

/* About box text */
data 'TEXT' (128) {
    "AI Assistant\r\r"
    "AI for Macintosh\r\r"
    "This application provides AI capabilities for your Macintosh.\r\r"
    "Press Cmd-L to open the chat window."
};

/* About box window */
resource 'WIND' (128) {
    {40, 40, 260, 360}, altDBoxProc;
    invisible;
    noGoAway;
    0, "";
    noAutoCenter;
};

/* Main application window */
resource 'WIND' (129) {
    {40, 40, 340, 440}, documentProc;
    visible;
    goAway;
    0, "AI Assistant";
    centerMainScreen;
};

/* Chat window */
resource 'WIND' (130) {
    {50, 50, 400, 500}, documentProc;
    visible;  /* Change from invisible to visible */
    goAway;
    0, "AI Chat";
    centerMainScreen;
};

/* Error alert */
resource 'ALRT' (200) {
    {50, 50, 150, 350},
    200,
    {
        OK, visible, sound1,
        OK, visible, sound1,
        OK, visible, sound1,
        OK, visible, sound1
    },
    alertPositionMainScreen
};

resource 'DITL' (200) {
    {
        {70, 250, 90, 310},
        Button { enabled, "OK" };
        
        {10, 10, 60, 310},
        StaticText { enabled, "^0" };
    }
};

/* Memory allocation */
resource 'SIZE' (-1) {
    reserved,
    acceptSuspendResumeEvents,
    reserved,
    canBackground,
    doesActivateOnFGSwitch,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreChildDiedEvents,
    is32BitCompatible,
    isHighLevelEventAware,
    onlyLocalHLEvents,
    notStationeryAware,
    dontUseTextEditServices,
    reserved,
    reserved,
    reserved,
    512 * 1024,   /* Minimal memory size for Markov chain and conversation history */
    768 * 1024    /* Preferred memory size with buffer for generation and UI */
};