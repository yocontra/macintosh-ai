#include "beepbop.h"

/*
 * Tetris theme (Korobeiniki) optimized for Mac Plus sound capabilities
 *
 * The Mac Plus has an 8-bit mono 22KHz sound chip, and performs best
 * with mid-to-low range frequencies. Higher notes are more distinct,
 * and we need to give the sound system enough time between notes.
 */
static Note tetrisTheme[] = {
    // First part - use a shorter, simpler version
    {15, NOTE_E4},  // E4 is 330 Hz - good audible range for Mac Plus
    {10, NOTE_B3},
    {10, NOTE_C4},
    {10, NOTE_D4},
    {10, NOTE_C4},
    {10, NOTE_B3},
    {15, NOTE_A3},
    {10, NOTE_A3},
    {15, NOTE_C4},
    {10, NOTE_E4},
    {10, NOTE_D4},
    {10, NOTE_C4},
    {15, NOTE_B3},
    {10, NOTE_C4},
    {10, NOTE_D4},
    {15, NOTE_E4},
    {15, NOTE_C4},
    {15, NOTE_A3},
    {15, NOTE_REST},  // Rest between sections

    // Loop back to beginning
    {0, NOTE_LOOP}};


/* Start playing the Tetris theme song */
void TetrisPlayMusic(void)
{
    // BeepBopPlay will auto-initialize the sound system
    // Use a very simple approach with minimal parameters
    BeepBopPlay(tetrisTheme, 8);  // Longer delay between notes
}

/* Stop the currently playing music */
void TetrisStopMusic(void)
{
    BeepBopStop();
}

/* Clean up audio resources - now a simple pass-through to BeepBopCleanup */
void TetrisAudioCleanup(void)
{
    BeepBopCleanup();
}