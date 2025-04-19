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
        "-", noIcon, noKey, noMark, plain;
        "Quit", noIcon, "Q", noMark, plain;
    }
};

/* Models menu */
resource 'MENU' (130) {
    130, textMenuProc;
    allEnabled, enabled;
    "Models";
    {
        "Markov Chain", noIcon, noKey, noMark, plain;
        "OpenAI", noIcon, noKey, noMark, plain;
        "Template", noIcon, noKey, noMark, plain;
    }
};

/* Extras menu */
resource 'MENU' (131) {
    131, textMenuProc;
    allEnabled, enabled;
    "Extras";
    {
        "Play Music", noIcon, noKey, noMark, plain;
    }
};

/* Menu bar definition */
resource 'MBAR' (128) {
    { 128, 129, 130, 131 };
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

/* Application icon */
resource 'ICON' (128) {
    /* 32x32 1-bit icon for AI Assistant - speech bubble with "AI" inside */
    $"0000 0000 0000 0000"
    $"0001 FFFF FFFF 8000"
    $"000F FFFF FFFF F000"
    $"003F FFFF FFFF FC00"
    $"007F FFFF FFFF FE00"
    $"00FF FFFF FFFF FF00"
    $"01FF FFFF FFFF FF80"
    $"01FF C000 000F FF80"
    $"03FF 0000 0003 FFC0"
    $"03FE 0000 0001 FFC0"
    $"07FC 0000 0000 FFE0"
    $"07FC 0000 0000 7FE0"
    $"07F8 0000 0000 3FE0"
    $"0FF8 0000 0000 1FF0"
    $"0FF8 03FF FC00 1FF0"
    $"0FF8 0FFF FF00 1FF0"
    $"0FF8 1FFF FF80 1FF0"
    $"0FF8 3FE0 3FC0 1FF0"
    $"0FF8 3FC0 1FC0 1FF0"
    $"0FF8 7F80 0FE0 1FF0"
    $"0FF8 7F80 0FE0 1FF0"
    $"0FF8 7F80 0FE0 1FF0"
    $"0FF8 7F80 0FE0 1FF0"
    $"0FF8 3FC0 1FC0 1FF0"
    $"0FF8 3FE0 3FC0 1FF0"
    $"0FF8 1FFF FF80 1FF0"
    $"0FF8 0FFF FF00 1FF0"
    $"0FF8 03FF FC00 1FF0"
    $"0FF8 0000 0000 1FF0"
    $"0FFC 0000 0000 3FF0"
    $"07FF FFFF FFFF FFE0"
    $"03FF FFFF FFFF FFC0"
};

/* Application icon for Finder */
resource 'ICN#' (128) {
    {
        /* 32x32 icon */
        $"0000 0000 0000 0000"
        $"0001 FFFF FFFF 8000"
        $"000F FFFF FFFF F000"
        $"003F FFFF FFFF FC00"
        $"007F FFFF FFFF FE00"
        $"00FF FFFF FFFF FF00"
        $"01FF FFFF FFFF FF80"
        $"01FF C000 000F FF80"
        $"03FF 0000 0003 FFC0"
        $"03FE 0000 0001 FFC0"
        $"07FC 0000 0000 FFE0"
        $"07FC 0000 0000 7FE0"
        $"07F8 0000 0000 3FE0"
        $"0FF8 0000 0000 1FF0"
        $"0FF8 03FF FC00 1FF0"
        $"0FF8 0FFF FF00 1FF0"
        $"0FF8 1FFF FF80 1FF0"
        $"0FF8 3FE0 3FC0 1FF0"
        $"0FF8 3FC0 1FC0 1FF0"
        $"0FF8 7F80 0FE0 1FF0"
        $"0FF8 7F80 0FE0 1FF0"
        $"0FF8 7F80 0FE0 1FF0"
        $"0FF8 7F80 0FE0 1FF0"
        $"0FF8 3FC0 1FC0 1FF0"
        $"0FF8 3FE0 3FC0 1FF0"
        $"0FF8 1FFF FF80 1FF0"
        $"0FF8 0FFF FF00 1FF0"
        $"0FF8 03FF FC00 1FF0"
        $"0FF8 0000 0000 1FF0"
        $"0FFC 0000 0000 3FF0"
        $"07FF FFFF FFFF FFE0"
        $"03FF FFFF FFFF FFC0",
        
        /* Mask (same as icon for simplicity) */
        $"0000 0000 0000 0000"
        $"0001 FFFF FFFF 8000"
        $"000F FFFF FFFF F000"
        $"003F FFFF FFFF FC00"
        $"007F FFFF FFFF FE00"
        $"00FF FFFF FFFF FF00"
        $"01FF FFFF FFFF FF80"
        $"01FF C000 000F FF80"
        $"03FF 0000 0003 FFC0"
        $"03FE 0000 0001 FFC0"
        $"07FC 0000 0000 FFE0"
        $"07FC 0000 0000 7FE0"
        $"07F8 0000 0000 3FE0"
        $"0FF8 0000 0000 1FF0"
        $"0FF8 03FF FC00 1FF0"
        $"0FF8 0FFF FF00 1FF0"
        $"0FF8 1FFF FF80 1FF0"
        $"0FF8 3FE0 3FC0 1FF0"
        $"0FF8 3FC0 1FC0 1FF0"
        $"0FF8 7F80 0FE0 1FF0"
        $"0FF8 7F80 0FE0 1FF0"
        $"0FF8 7F80 0FE0 1FF0"
        $"0FF8 7F80 0FE0 1FF0"
        $"0FF8 3FC0 1FC0 1FF0"
        $"0FF8 3FE0 3FC0 1FF0"
        $"0FF8 1FFF FF80 1FF0"
        $"0FF8 0FFF FF00 1FF0"
        $"0FF8 03FF FC00 1FF0"
        $"0FF8 0000 0000 1FF0"
        $"0FFC 0000 0000 3FF0"
        $"07FF FFFF FFFF FFE0"
        $"03FF FFFF FFFF FFC0"
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