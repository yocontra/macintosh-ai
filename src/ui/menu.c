#include <Devices.h>
#include <Events.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Windows.h>

#include "../constants.h"
#include "../error.h"
#include "menu.h"
#include "window_manager.h"

/*********************************************************************
 * MENU HANDLING FUNCTIONS
 *********************************************************************/

/* Update the menu state based on current application mode */
void UpdateMenus(void)
{
    MenuRef m   = GetMenu(kMenuFile);
    WindowRef w = FrontWindow();

    /* First determine which window is in front to properly set menu items */
    switch (gAppMode) {
    case kModeMainSplash:
        /* In splash mode, Close is disabled unless a window is open */
        if (w && w == WindowManager_GetWindowRef(kWindowTypeSplash))
            EnableItem(m, kItemClose); /* Can close main window */
        else
            DisableItem(m, kItemClose);

        /* Always enable Chat menu item in splash mode */
        EnableItem(m, kItemChat);
        break;

    case kModeChatWindow:
        /* In chat mode, Close will close the chat window */
        if (w && w == WindowManager_GetWindowRef(kWindowTypeChat))
            EnableItem(m, kItemClose); /* Can close chat window */
        else
            DisableItem(m, kItemClose);

        /* Chat is already open, so no need to enable/disable it */
        break;
    }

    /* Handle Edit menu based on what's in front */
    m = GetMenu(kMenuEdit);
    if (w && GetWindowKind(w) < 0) {
        /* Desk accessory in front: Enable edit menu items */
        EnableItem(m, 1);
        EnableItem(m, 3);
        EnableItem(m, 4);
        EnableItem(m, 5);
        EnableItem(m, 6);
    }
    else if (gAppMode == kModeChatWindow && WindowManager_IsWindowVisible(kWindowTypeChat) &&
             w == WindowManager_GetWindowRef(kWindowTypeChat)) {
        /* Chat window is in front: Enable copy and possibly other edit operations */
        EnableItem(m, 4); /* Copy */

        /* Also enable the input field related operations */
        EnableItem(m, 1); /* Undo */
        EnableItem(m, 3); /* Cut */
        EnableItem(m, 5); /* Paste */
        EnableItem(m, 6); /* Clear */
    }
    else {
        /* Other window or nothing in front, disable edit menu */
        DisableItem(m, 1);
        DisableItem(m, 3);
        DisableItem(m, 4);
        DisableItem(m, 5);
        DisableItem(m, 6);
    }
}

/* Handle menu commands */
void DoMenuCommand(long menuCommand)
{
    Str255 str;
    WindowRef w;
    short menuID   = menuCommand >> 16;
    short menuItem = menuCommand & 0xFFFF;

    if (menuID == kMenuApple) {
        if (menuItem == kItemAbout)
            WindowManager_OpenWindow(kWindowTypeAbout);
        else {
            GetMenuItemText(GetMenu(128), menuItem, str);
            OpenDeskAcc(str);
        }
    }
    else if (menuID == kMenuFile) {
        switch (menuItem) {
        case kItemChat:
            /* This switches between modes */
            if (gAppMode == kModeMainSplash || !WindowManager_IsWindowVisible(kWindowTypeChat)) {
                /* Open chat window and close splash window */
                WindowManager_OpenWindow(kWindowTypeChat);
                WindowManager_CloseWindow(kWindowTypeSplash);
            }
            else {
                /* Already in chat mode, bring window to front */
                WindowRef w = WindowManager_GetWindowRef(kWindowTypeChat);
                if (w != NULL) {
                    SelectWindow(w);
                }
            }
            break;

        case kItemClose:
            w = FrontWindow();
            if (w) {
                if (GetWindowKind(w) < 0) {
                    /* Close desk accessory */
                    CloseDeskAcc(GetWindowKind(w));
                }
                else if (w == WindowManager_GetWindowRef(kWindowTypeChat)) {
                    /* Close chat window and switch back to splash mode */
                    WindowManager_CloseWindow(kWindowTypeChat);
                    WindowManager_OpenWindow(kWindowTypeSplash);
                }
                else if (w == WindowManager_GetWindowRef(kWindowTypeSplash)) {
                    /* Close main window (not normally done - usually = quit) */
                    QuitApplication(false);
                }
                else {
                    /* Other window */
                    DisposeWindow(w);
                }
            }
            break;

        case kItemQuit:
            /* Clean up resources before quitting using standard function */
            QuitApplication(false);
            break;
        }
    }
    else if (menuID == kMenuEdit) {
        if (!SystemEdit(menuItem - 1)) {
            /* Edit command not handled by desk accessory */
            w = FrontWindow();
            if (WindowManager_IsWindowVisible(kWindowTypeChat) &&
                w == WindowManager_GetWindowRef(kWindowTypeChat)) {
                /* Handle all edit menu operations in chat window */
                switch (menuItem) {
                case 1: /* Undo - not supported in TextEdit */
                    break;
                case 3: /* Cut */
                    TECut(gChatInputTE);
                    break;
                case 4: /* Copy */
                    /*
                     * For TextEdit operations, we still need to access the TE handles directly
                     * This is a limitation of the current architecture, as TextEdit handles
                     * are not abstracted through the window manager.
                     */
                    if (gChatDisplayTE != NULL &&
                        (*gChatDisplayTE)->selStart < (*gChatDisplayTE)->selEnd) {
                        /* Display TE has selection, use it for copy */
                        TECopy(gChatDisplayTE);
                    }
                    else if (gChatInputTE != NULL) {
                        /* Default to input field */
                        TECopy(gChatInputTE);
                    }
                    break;
                case 5: /* Paste */
                    TEPaste(gChatInputTE);
                    break;
                case 6: /* Clear */
                    TEDelete(gChatInputTE);
                    break;
                }
            }
        }
    }

    HiliteMenu(0);
}