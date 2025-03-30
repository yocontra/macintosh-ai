#include "beepbop.h"

// Global sound channel pointer
static SndChannelPtr gSoundChannel = NULL;

// Global playback state
static struct {
    Note *melody;             // Pointer to melody array
    short currentNoteIndex;   // Current position in melody
    short delayBetweenNotes;  // Delay between notes
    Boolean isPlaying;        // Whether playback is active
    TimerUPP timerUPP;        // Timer callback UPP
    TMTask timerTask;         // Time Manager task
} gPlayback;

// Forward declaration of timer callback
static pascal void BeepBopTimerCallback(TMTaskPtr taskPtr);

// Initialize the BeepBop sound system
void BeepBopInit(void)
{
    OSErr error;

    // Create a sound channel
    error = SndNewChannel(&gSoundChannel, squareWaveSynth, 0, NULL);
    if (error != noErr) {
        gSoundChannel = NULL;
        return;
    }

    // Initialize playback state
    gPlayback.melody           = NULL;
    gPlayback.currentNoteIndex = 0;
    gPlayback.isPlaying        = false;

    // Create timer UPP
    gPlayback.timerUPP = NewTimerUPP(BeepBopTimerCallback);

    // Initialize the timer task
    gPlayback.timerTask.tmAddr     = gPlayback.timerUPP;
    gPlayback.timerTask.tmCount    = 0;
    gPlayback.timerTask.tmWakeUp   = 0;
    gPlayback.timerTask.tmReserved = 0;
}

// Timer callback for background music playback
static pascal void BeepBopTimerCallback(TMTaskPtr taskPtr)
{
    Note currentNote;
    short nextDelay;

    if (!gPlayback.isPlaying || gSoundChannel == NULL || gPlayback.melody == NULL) {
        return;
    }

    // Get the current note
    currentNote = gPlayback.melody[gPlayback.currentNoteIndex];

    // Check for end of melody (duration of 0)
    if (currentNote.duration == 0) {
        if (currentNote.pitch == NOTE_LOOP) {
            // Loop back to beginning
            gPlayback.currentNoteIndex = 0;
            currentNote                = gPlayback.melody[0];
        }
        else {
            // End of melody (no loop marker)
            gPlayback.isPlaying = false;
            return;
        }
    }

    // Play the current note
    if (currentNote.pitch == NOTE_REST) {
        // For a rest, use a quiet command
        SndCommand cmd;
        cmd.cmd    = quietCmd;
        cmd.param1 = 0;
        cmd.param2 = 0;
        SndDoCommand(gSoundChannel, &cmd, false);
    }
    else {
        // For a normal note, use frequency/duration command
        SndCommand cmd;
        cmd.cmd    = freqDurationCmd;
        cmd.param1 = currentNote.pitch;
        cmd.param2 = currentNote.duration * 60;
        SndDoCommand(gSoundChannel, &cmd, false);
    }

    // Move to the next note
    gPlayback.currentNoteIndex++;

    // Calculate delay before next note
    nextDelay = currentNote.duration + gPlayback.delayBetweenNotes;

    // Schedule the next note
    if (gPlayback.isPlaying) {
        // Reset task fields
        taskPtr->tmCount    = 0;
        taskPtr->tmWakeUp   = 0;
        taskPtr->tmReserved = 0;

        // Insert the timer task again
        InsTime((QElemPtr)taskPtr);
        PrimeTime((QElemPtr)taskPtr, nextDelay);
    }
}

// Start playing background music
void BeepBopPlay(Note *notes, short delayBetweenNotes)
{
    if (gSoundChannel == NULL || notes == NULL)
        return;

    // Stop any existing playback
    BeepBopStop();

    // Set up the new playback
    gPlayback.melody            = notes;
    gPlayback.currentNoteIndex  = 0;
    gPlayback.delayBetweenNotes = delayBetweenNotes;
    gPlayback.isPlaying         = true;

    // Start the timer for the first note
    InsTime((QElemPtr)&gPlayback.timerTask);
    PrimeTime((QElemPtr)&gPlayback.timerTask, 1);
}

// Check if music is currently playing
Boolean BeepBopIsPlaying(void)
{
    return gPlayback.isPlaying;
}

// Stop any currently playing melody
void BeepBopStop(void)
{
    if (!gPlayback.isPlaying)
        return;

    // Cancel the timer
    RmvTime((QElemPtr)&gPlayback.timerTask);

    // Send a quiet command to stop any playing sound
    if (gSoundChannel != NULL) {
        SndCommand cmd;
        cmd.cmd    = quietCmd;
        cmd.param1 = 0;
        cmd.param2 = 0;
        SndDoCommand(gSoundChannel, &cmd, false);
    }

    // Reset playback state
    gPlayback.isPlaying = false;
}

// Clean up sound resources
void BeepBopCleanup(void)
{
    BeepBopStop();

    if (gPlayback.timerUPP != NULL) {
        DisposeTimerUPP(gPlayback.timerUPP);
        gPlayback.timerUPP = NULL;
    }

    if (gSoundChannel != NULL) {
        SndDisposeChannel(gSoundChannel, false);
        gSoundChannel = NULL;
    }
}