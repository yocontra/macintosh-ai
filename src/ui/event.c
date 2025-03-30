#include <Events.h>
#include <Menus.h>
#include <Quickdraw.h>
#include <Windows.h>

#include "../constants.h"
#include "../error.h"
#include "../nuklear/nuklear.h" /* Include only header without implementation */
#include "chat_window.h"
#include "event.h"
#include "splash_window.h"

/* External references to the Nuklear context and functions */
extern struct nk_context *ctx;
extern void nk_quickdraw_shutdown(void);

/*********************************************************************
 * APPLICATION CONTROL FUNCTIONS
 *********************************************************************/

/* Quit the application */
void QuitApplication(Boolean saveChanges)
{
    /* We don't have document changes to save, so parameter not used */
    (void)saveChanges;

    /* Clean up windows */
    ChatWindow_Dispose();
    SplashWindow_Dispose();

    /* Clean up Nuklear UI system */
    if (ctx) {
        nk_quickdraw_shutdown();
        ctx = NULL;
    }

    /* Exit application */
    ExitToShell();
}