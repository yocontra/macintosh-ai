#ifndef TETRIS_AUDIO_H
#define TETRIS_AUDIO_H

/* Start playing the Tetris theme song */
void TetrisPlayMusic(void);

/* Stop the currently playing music */
void TetrisStopMusic(void);

/* Clean up audio resources */
void TetrisAudioCleanup(void);

#endif