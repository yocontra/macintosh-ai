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
        "Settings...", noIcon, ",", noMark, plain;
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
        "Remote AI", noIcon, noKey, noMark, plain;
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

/* About box (modal) */
resource 'DLOG' (128) {
    {60, 60, 250, 470},
    dBoxProc,
    invisible,
    noGoAway,
    0,
    128,
    "About Macintosh AI",
    centerMainScreen
};

resource 'DITL' (128) {
    {
        {150, 155, 170, 225},
        Button { enabled, "OK" };

        {14, 16, 32, 394},
        StaticText { enabled, "Macintosh AI" };

        {36, 16, 52, 394},
        StaticText { enabled, "Version ^0" };

        {64, 16, 80, 394},
        StaticText { enabled, "AI chat for the Macintosh." };

        {84, 16, 100, 394},
        StaticText { enabled, "Requires a 68020 or later, 8 MB of memory," };

        {104, 16, 120, 394},
        StaticText { enabled, "System 7.5.3 or later, and Open Transport 1.3." };

        {128, 16, 144, 394},
        StaticText { enabled, "Set your provider, model and API key in File > Settings." };
    }
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

/* Settings dialog (modal) */
resource 'DLOG' (140) {
    {80, 60, 260, 460},
    dBoxProc,
    invisible,
    noGoAway,
    0,
    140,
    "AI Settings",
    centerMainScreen
};

resource 'DITL' (140) {
    {
        {10, 12, 24, 180},
        StaticText { enabled, "^0" };

        {8, 190, 26, 388},
        EditText { enabled, "anthropic" };

        {34, 12, 48, 180},
        StaticText { enabled, "^1" };

        {32, 190, 50, 388},
        EditText { enabled, "claude-sonnet-4-5" };

        {58, 12, 72, 180},
        StaticText { enabled, "^2" };

        {56, 190, 74, 388},
        EditText { enabled, "" };

        {82, 12, 96, 180},
        StaticText { enabled, "^3" };

        {80, 190, 98, 388},
        EditText { enabled, "" };

        {128, 250, 148, 310},
        Button { enabled, "OK" };

        {128, 320, 148, 380},
        Button { enabled, "Cancel" };
    }
};

/* Error alert (the ALRT stage list is always four entries) */
resource 'ALRT' (200) {
    {40, 40, 150, 500},
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
        {70, 350, 90, 410},
        Button { enabled, "OK" };

        {10, 10, 42, 470},
        StaticText { enabled, "^1" };

        {46, 10, 66, 470},
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

/* Memory allocation.
 *
 * A SIZE resource needs all sixteen flag bits plus the two sizes: the flags
 * are mutually exclusive pairs, so dropping one (we remove
 * dontUseTextEditServices because the app allocates TextEdit records) means
 * supplying its partner, not deleting the slot.
 */
resource 'SIZE' (-1) {
    reserved,
    acceptSuspendResumeEvents,
    reserved,
    canBackground,
    doesActivateOnFGSwitch,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreAppDiedEvents,
    is32BitCompatible,
    isHighLevelEventAware,
    onlyLocalHLEvents,
    notStationeryAware,
    useTextEditServices,
    notDisplayManagerAware,
    reserved,
    reserved,
    3072 * 1024,  /* Suggested: ~938 KB of statics, ~600 KB of code, 128 KB stack */
    3072 * 1024   /* Minimum: below this the 68k loader cannot map code plus heap */
};