/* BeepBop.h */
#ifndef BEEP_BOP_H
#define BEEP_BOP_H

#include <Events.h>
#include <Memory.h>
#include <Sound.h>
#include <Timer.h>

/*
 * BeepBop - An 8-bit sound library for Macintosh Plus
 *
 * A simple library for playing background music using the
 * Macintosh Plus sound hardware.
 */

// Special signals
#define NOTE_REST 0   // Rest note (silence)
#define NOTE_LOOP -1  // Loop back to beginning of melody

// Octave 2 (Low bass notes)
#define NOTE_C2 65
#define NOTE_CS2 69
#define NOTE_D2 73
#define NOTE_DS2 78
#define NOTE_E2 82
#define NOTE_F2 87
#define NOTE_FS2 93
#define NOTE_G2 98
#define NOTE_GS2 104
#define NOTE_A2 110
#define NOTE_AS2 117
#define NOTE_B2 123

// Octave 3
#define NOTE_C3 131
#define NOTE_CS3 139
#define NOTE_D3 147
#define NOTE_DS3 156
#define NOTE_E3 165
#define NOTE_F3 175
#define NOTE_FS3 185
#define NOTE_G3 196
#define NOTE_GS3 208
#define NOTE_A3 220
#define NOTE_AS3 233
#define NOTE_B3 247

// Octave 4 (Middle octave)
#define NOTE_C4 262
#define NOTE_CS4 277
#define NOTE_D4 294
#define NOTE_DS4 311
#define NOTE_E4 330
#define NOTE_F4 349
#define NOTE_FS4 370
#define NOTE_G4 392
#define NOTE_GS4 415
#define NOTE_A4 440  // A440 (concert pitch)
#define NOTE_AS4 466
#define NOTE_B4 494

// Octave 5
#define NOTE_C5 523
#define NOTE_CS5 554
#define NOTE_D5 587
#define NOTE_DS5 622
#define NOTE_E5 659
#define NOTE_F5 698
#define NOTE_FS5 740
#define NOTE_G5 784
#define NOTE_GS5 831
#define NOTE_A5 880
#define NOTE_AS5 932
#define NOTE_B5 988

// Octave 6
#define NOTE_C6 1047
#define NOTE_CS6 1109
#define NOTE_D6 1175
#define NOTE_DS6 1245
#define NOTE_E6 1319
#define NOTE_F6 1397
#define NOTE_FS6 1480
#define NOTE_G6 1568
#define NOTE_GS6 1661
#define NOTE_A6 1760
#define NOTE_AS6 1865
#define NOTE_B6 1976

// Octave 7 (High range)
#define NOTE_C7 2093
#define NOTE_CS7 2217
#define NOTE_D7 2349
#define NOTE_DS7 2489
#define NOTE_E7 2637
#define NOTE_F7 2794
#define NOTE_FS7 2960
#define NOTE_G7 3136
#define NOTE_GS7 3322
#define NOTE_A7 3520
#define NOTE_AS7 3729
#define NOTE_B7 3951

// Structure for representing a musical note
typedef struct {
    short duration;  // Duration in ticks (1/60th of a second)
    short pitch;     // Frequency in Hz or special signal
} Note;

// Initialize the BeepBop sound system
void BeepBopInit(void);

// Start playing background music with optional delay between notes
void BeepBopPlay(Note *notes, short delayBetweenNotes);

// Check if music is currently playing
Boolean BeepBopIsPlaying(void);

// Stop any currently playing melody
void BeepBopStop(void);

// Clean up sound resources
void BeepBopCleanup(void);

#endif /* BEEP_BOP_H */