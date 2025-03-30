#include <Devices.h>
#include <Events.h>
#include <Menus.h>
#include <Quickdraw.h>
#include <Sound.h>
#include <TextEdit.h>
#include <Windows.h>

#include "../constants.h"
#include "../error.h"
#include "about_window.h"
#include "chat_window.h"
#include "menu.h"
#include "splash_window.h"
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
        if (w && w == SplashWindow_GetWindowRef())
            EnableItem(m, kItemClose); /* Can close main window */
        else
            DisableItem(m, kItemClose);

        /* Always enable Chat menu item in splash mode */
        EnableItem(m, kItemChat);
        break;

    case kModeChatWindow:
        /* In chat mode, Close will close the chat window */
        if (w && w == ChatWindow_GetWindowRef())
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
    else if (gAppMode == kModeChatWindow && ChatWindow_IsVisible() &&
             w == ChatWindow_GetWindowRef()) {
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
            ShowAboutBox();
        else {
            GetMenuItemText(GetMenu(128), menuItem, str);
            OpenDeskAcc(str);
        }
    }
    else if (menuID == kMenuFile) {
        switch (menuItem) {
        case kItemChat:
            /* This switches between modes */
            if (gAppMode == kModeMainSplash || !ChatWindow_IsVisible()) {
                /* Open chat window and switch to chat mode */
                ChatWindow_Show(true);
                SplashWindow_Show(false);
                gAppMode = kModeChatWindow;
            }
            else {
                /* Already in chat mode, bring window to front */
                SelectWindow(ChatWindow_GetWindowRef());
            }
            break;

        case kItemClose:
            w = FrontWindow();
            if (w) {
                if (GetWindowKind(w) < 0) {
                    /* Close desk accessory */
                    CloseDeskAcc(GetWindowKind(w));
                }
                else if (w == ChatWindow_GetWindowRef()) {
                    /* Close chat window and switch back to splash mode */
                    ChatWindow_Show(false);
                    SplashWindow_Show(true);
                    gAppMode = kModeMainSplash;
                }
                else if (w == SplashWindow_GetWindowRef()) {
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
            if (ChatWindow_IsVisible() && w == ChatWindow_GetWindowRef()) {
                /* Handle all edit menu operations in chat window */
                switch (menuItem) {
                case 1: /* Undo - not supported in TextEdit */
                    break;
                case 3: /* Cut */
                    TECut(gChatInputTE);
                    break;
                case 4: /* Copy */
                    /* Get the active text field based on which one has a selection */
                    TEHandle activeTE  = ChatWindow_GetInputTE();
                    TEHandle displayTE = ChatWindow_GetDisplayTE();

                    if (displayTE != NULL && (*displayTE)->selStart < (*displayTE)->selEnd) {
                        /* Display TE has selection, use it for copy */
                        TECopy(displayTE);
                    }
                    else if (activeTE != NULL) {
                        /* Default to input field */
                        TECopy(activeTE);
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