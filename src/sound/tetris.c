#include "beepbop.h"

/* Tetris theme (Korobeiniki) with loop */
static Note tetrisTheme[] = {
    // Main melody
    {12, NOTE_E5},
    {6, NOTE_B4},
    {6, NOTE_C5},
    {6, NOTE_D5},
    {6, NOTE_C5},
    {6, NOTE_B4},
    {12, NOTE_A4},
    {6, NOTE_A4},
    {12, NOTE_C5},
    {6, NOTE_E5},
    {6, NOTE_D5},
    {6, NOTE_C5},
    {12, NOTE_B4},
    {6, NOTE_B4},
    {6, NOTE_C5},
    {6, NOTE_D5},
    {12, NOTE_E5},
    {12, NOTE_C5},
    {12, NOTE_A4},
    {12, NOTE_A4},
    {24, NOTE_REST},

    // Second part
    {12, NOTE_D5},
    {6, NOTE_F5},
    {6, NOTE_A5},
    {6, NOTE_G5},
    {6, NOTE_F5},
    {12, NOTE_E5},
    {6, NOTE_C5},
    {6, NOTE_E5},
    {12, NOTE_D5},
    {6, NOTE_C5},
    {6, NOTE_B4},
    {12, NOTE_B4},
    {6, NOTE_C5},
    {6, NOTE_D5},
    {12, NOTE_E5},
    {12, NOTE_C5},
    {12, NOTE_A4},
    {12, NOTE_A4},
    {24, NOTE_REST},

    // Loop back to beginning
    {0, NOTE_LOOP}};

/* Initialize the Tetris music system */
void TetrisAudioInit(void)
{
    BeepBopInit();
}

/* Start playing the Tetris theme song */
void TetrisPlayMusic(void)
{
    BeepBopPlay(tetrisTheme, 1);
}

/* Stop the currently playing music */
void TetrisStopMusic(void)
{
    BeepBopStop();
}

/* Clean up audio resources */
void TetrisAudioCleanup(void)
{
    BeepBopCleanup();
}