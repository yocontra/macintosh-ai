/*
 * BeepBop.c - Sound playback library for Macintosh Plus/System 7
 *
 * A reliable implementation for playing musical notes using
 * the Sound Manager in System 7.0.1.
 */

#include "beepbop.h"
#include "../constants.h"
#include "../error.h"
#include <Sound.h>

/* Mac Plus sound hardware constants */
#define MAX_SOUND_FREQUENCY 2000 /* Upper frequency limit for Mac Plus */
#define MIN_SOUND_FREQUENCY 100  /* Lower frequency limit */
#define DEFAULT_DURATION 12      /* Duration in ticks (1/5 second) */

/* Sound channel for playback */
static SndChannelPtr gSoundChannel = NULL;

/* Sound playback state */
static struct {
    Note *melody;             /* Pointer to melody (array of notes) */
    short currentNoteIndex;   /* Current position in melody */
    short delayBetweenNotes;  /* Extra delay between notes in ticks */
    short ticksUntilNextNote; /* Counter for timing next note */
    Boolean isPlaying;        /* Whether playback is active */
    Boolean soundInitialized; /* Whether sound system is ready */
} gPlayback = {NULL, 0, 0, 0, false, false};

/* Clamp a frequency value within the supported range */
static short ClampFrequency(short freq)
{
    if (freq < MIN_SOUND_FREQUENCY)
        return MIN_SOUND_FREQUENCY;
    if (freq > MAX_SOUND_FREQUENCY)
        return MAX_SOUND_FREQUENCY;
    return freq;
}

/* Initialize the BeepBop sound system */
void BeepBopInit(void)
{
    OSErr error;
    SndCommand cmd;

    /* Reset playback state */
    gPlayback.melody             = NULL;
    gPlayback.currentNoteIndex   = 0;
    gPlayback.delayBetweenNotes  = 0;
    gPlayback.ticksUntilNextNote = 0;
    gPlayback.isPlaying          = false;
    gPlayback.soundInitialized   = false;

    /* Create a new sound channel with square wave synthesis */
    error = SndNewChannel(&gSoundChannel, squareWaveSynth, 0, NULL);
    if (error != noErr) {
        /* If we couldn't create a sound channel, we'll fall back to SysBeep */
        gSoundChannel = NULL;
        return;
    }

    /* Initialize the sound channel */
    cmd.cmd    = flushCmd;
    cmd.param1 = 0;
    cmd.param2 = 0;
    SndDoImmediate(gSoundChannel, &cmd);

    /* Set maximum volume */
    cmd.cmd    = ampCmd;
    cmd.param1 = 0;
    cmd.param2 = 255;
    SndDoImmediate(gSoundChannel, &cmd);

    /* Mark sound as initialized */
    gPlayback.soundInitialized = true;
}

/* Play a specific note using either Sound Manager or SysBeep */
static void PlayNote(short pitch)
{
    SndCommand cmd;
    OSErr error;

    /* For rest notes, just silence the channel */
    if (pitch == NOTE_REST) {
        if (gSoundChannel != NULL) {
            cmd.cmd    = quietCmd;
            cmd.param1 = 0;
            cmd.param2 = 0;
            SndDoImmediate(gSoundChannel, &cmd);
        }
        return;
    }

    /* Normalize pitch to the supported range */
    pitch = ClampFrequency(pitch);

    /* Try to use Sound Manager if available */
    if (gSoundChannel != NULL) {
        /* First, make sure the channel is quiet */
        cmd.cmd    = quietCmd;
        cmd.param1 = 0;
        cmd.param2 = 0;
        SndDoImmediate(gSoundChannel, &cmd);

        /* Set maximum volume */
        cmd.cmd    = ampCmd;
        cmd.param1 = 0;
        cmd.param2 = 255; /* Maximum volume */
        SndDoImmediate(gSoundChannel, &cmd);

        /* Set the frequency */
        cmd.cmd    = freqCmd;
        cmd.param1 = 0;
        cmd.param2 = pitch;
        SndDoImmediate(gSoundChannel, &cmd);

        /* Start the sound - simplified approach */
        cmd.cmd    = soundCmd;
        cmd.param1 = 1; /* Short duration */
        cmd.param2 = 0; /* No special flags */
        SndDoImmediate(gSoundChannel, &cmd);
        return;
    }
    /* If no sound channel, use SysBeep with fixed duration - works on all systems */
    SysBeep(1);
}

/* Process the next note in the melody */
static void ProcessNextNote(void)
{
    Note currentNote;

    /* Safety checks */
    if (!gPlayback.isPlaying || !gPlayback.melody) {
        return;
    }

    /* Get the current note */
    currentNote = gPlayback.melody[gPlayback.currentNoteIndex];

    /* Check for special markers */
    if (currentNote.duration == 0) {
        /* End of sequence or loop marker */
        if (currentNote.pitch == NOTE_LOOP) {
            /* Loop back to beginning */
            gPlayback.currentNoteIndex = 0;
            currentNote                = gPlayback.melody[0];
        }
        else {
            /* End of melody (reaching any zero duration note that's not a loop) */
            BeepBopStop();
            return;
        }
    }

    /* Play the current note */
    PlayNote(currentNote.pitch);

    /* Set up timing for the next note */
    gPlayback.ticksUntilNextNote = currentNote.duration + gPlayback.delayBetweenNotes;

    /* Move to the next note */
    gPlayback.currentNoteIndex++;
}

/* Start playing a melody */
void BeepBopPlay(Note *notes, short delayBetweenNotes)
{
    /* Validate parameters */
    if (notes == NULL) {
        return;
    }

    /* Auto-initialize if not already done */
    if (!gPlayback.soundInitialized) {
        BeepBopInit();
    }

    /* Stop any current playback */
    BeepBopStop();

    /* Set up new playback */
    gPlayback.melody             = notes;
    gPlayback.currentNoteIndex   = 0;
    gPlayback.delayBetweenNotes  = (delayBetweenNotes < 0) ? 0 : delayBetweenNotes;
    gPlayback.ticksUntilNextNote = 0;
    gPlayback.isPlaying          = true;

    /* Play the first note immediately */
    ProcessNextNote();
}

/* Check if music is currently playing */
Boolean BeepBopIsPlaying(void)
{
    return gPlayback.isPlaying;
}

/* Process sound playback - call regularly from main event loop */
void BeepBopIdle(void)
{
    static unsigned long lastTicks = 0;
    unsigned long currentTicks;
    unsigned long elapsedTicks;

    /* Skip if not playing */
    if (!gPlayback.isPlaying) {
        return;
    }

    /* Get current time */
    currentTicks = TickCount();

    /* Calculate elapsed time since last call */
    if (lastTicks == 0) {
        elapsedTicks = 1;
    }
    else {
        elapsedTicks = currentTicks - lastTicks;
    }
    lastTicks = currentTicks;

    /* Update timing counter */
    if (gPlayback.ticksUntilNextNote <= elapsedTicks) {
        /* Time for next note */
        ProcessNextNote();
    }
    else {
        /* Keep counting down */
        gPlayback.ticksUntilNextNote -= elapsedTicks;
    }
}

/* Stop the currently playing melody */
void BeepBopStop(void)
{
    /* Skip if not playing */
    if (!gPlayback.isPlaying) {
        return;
    }

    /* Silence the channel if we're using Sound Manager */
    if (gSoundChannel != NULL) {
        SndCommand cmd;
        cmd.cmd    = quietCmd;
        cmd.param1 = 0;
        cmd.param2 = 0;
        SndDoImmediate(gSoundChannel, &cmd);
    }

    /* Reset playback state */
    gPlayback.isPlaying          = false;
    gPlayback.currentNoteIndex   = 0;
    gPlayback.ticksUntilNextNote = 0;
}

/* Clean up sound resources */
void BeepBopCleanup(void)
{
    /* Stop playback */
    BeepBopStop();

    /* Dispose of sound channel if we created one */
    if (gSoundChannel != NULL) {
        SndDisposeChannel(gSoundChannel, false);
        gSoundChannel = NULL;
    }

    /* Reset state */
    gPlayback.soundInitialized = false;
}