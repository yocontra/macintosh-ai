#include <Events.h>
#include <Windows.h>

#include "../constants.h"
#include "../error.h"
#include "../sound/tetris.h"
#include "event.h"
#include "window_manager.h"

/*********************************************************************
 * APPLICATION CONTROL FUNCTIONS
 *********************************************************************/

/* Quit the application */
void QuitApplication(Boolean saveChanges)
{
    /* We don't have document changes to save, so parameter not used */
    (void)saveChanges;

    /* Stop any playing music and clean up sound resources */
    TetrisStopMusic();
    TetrisAudioCleanup();

    /* Clean up windows through the window manager */
    WindowManager_Dispose();

    /* Exit application */
    ExitToShell();
}