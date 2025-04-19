#include <Devices.h>
#include <Events.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Windows.h>

#include "../chatbot/model_manager.h"
#include "../constants.h"
#include "../error.h"
#include "../sound/tetris.h"
#include "chat_window.h"
#include "menu.h"
#include "window_manager.h"

/*********************************************************************
 * MENU HANDLING FUNCTIONS
 *********************************************************************/

/* Update the menu state based on current application mode */
void UpdateMenus(void)
{
    MenuRef fileMenu   = GetMenu(kMenuFile);
    MenuRef modelsMenu = GetMenu(kMenuModels);
    MenuRef extrasMenu = GetMenu(kMenuExtras);
    WindowRef w        = FrontWindow();

    /* First determine which window is in front to properly set menu items */
    switch (gAppMode) {
    case kModeMainSplash:
        /* In splash mode, Close is disabled unless a window is open */
        if (w && w == WindowManager_GetWindowRef(kWindowTypeSplash))
            EnableItem(fileMenu, kItemClose); /* Can close main window */
        else
            DisableItem(fileMenu, kItemClose);

        /* Always enable Chat menu item in splash mode */
        EnableItem(fileMenu, kItemChat);

        /* Disable Models menu in splash mode */
        DisableItem(modelsMenu, 0); /* Disable entire menu */
        break;

    case kModeChatWindow:
        /* In chat mode, Close will close the chat window */
        if (w && w == WindowManager_GetWindowRef(kWindowTypeChat))
            EnableItem(fileMenu, kItemClose); /* Can close chat window */
        else
            DisableItem(fileMenu, kItemClose);

        /* Enable Models menu in chat mode */
        EnableItem(modelsMenu, 0); /* Enable entire menu */

        /* Set checkmarks for active model */
        CheckItem(modelsMenu, kItemMarkovModel, gActiveAIModel == kMarkovModel);
        CheckItem(modelsMenu, kItemOpenAIModel, gActiveAIModel == kOpenAIModel);
        CheckItem(modelsMenu, kItemTemplateModel, gActiveAIModel == kTemplateModel);

        break;
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
    else if (menuID == kMenuModels) {
        /* Only change model when in chat mode */
        if (gAppMode == kModeChatWindow && WindowManager_IsWindowVisible(kWindowTypeChat)) {
            /* Get current chat window */
            WindowRef chatWindow = WindowManager_GetWindowRef(kWindowTypeChat);
            if (chatWindow != NULL) {
                /* Handle model selection */
                switch (menuItem) {
                case kItemMarkovModel:
                    /* Set to Markov chain model */
                    SetActiveAIModel(kMarkovModel);
                    /* Add message about model switch */
                    ChatWindow_AddMessage("Switched to Markov chain model.", false);
                    break;

                case kItemOpenAIModel:
                    /* Set to OpenAI model */
                    SetActiveAIModel(kOpenAIModel);
                    ChatWindow_AddMessage("Switched to OpenAI model.", false);
                    break;

                case kItemTemplateModel:
                    /* Set to Template model */
                    SetActiveAIModel(kTemplateModel);
                    ChatWindow_AddMessage("Switched to Template-based model.", false);
                    break;
                }

                /* Update menu to show check mark next to active model */
                UpdateMenus();
            }
        }
    }
    else if (menuID == kMenuExtras) {
        switch (menuItem) {
        case kItemPlayMusic:
            /* Play the tetris theme music */
            TetrisPlayMusic();
            break;
        }
    }

    HiliteMenu(0);
}