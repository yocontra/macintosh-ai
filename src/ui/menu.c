#include <Devices.h>
#include <Dialogs.h>
#include <Events.h>
#include <Fonts.h>
#include <Menus.h>
#include <TextEdit.h>
#include <TextUtils.h>
#include <Windows.h>
#include <string.h>

#include "../chatbot/api_client.h"
#include "../chatbot/model_manager.h"
#include "../constants.h"
#include "utils.h"
#include "../error.h"
#include "../sound/tetris.h"
#include "about_window.h"
#include "chat_window.h"
#include "menu.h"
#include "window_manager.h"

/*********************************************************************
 * MENU HANDLING FUNCTIONS
 *********************************************************************/

/* File menu item numbers (see MENU 129 in main.r) */
enum { kItemSettings = 3 };

/* Settings dialog items (see DLOG/DITL 140 in main.r) */
enum {
    kSettingsProviderEdit = 2,
    kSettingsModelEdit    = 4,
    kSettingsKeyEdit      = 6,
    kSettingsURLEdit      = 8,
    kSettingsOK           = 9,
    kSettingsCancel       = 10
};

/* Grey note under the base URL field, drawn by us at Geneva 9 (a DITL
   StaticText uses the dialog font, which is too large for small print). */
enum {
    kSettingsNoteLeft      = 16,
    kSettingsNoteBaseline1 = 183,
    kSettingsNoteBaseline2 = 194,
    kSettingsNoteSize      = 9
};

/* Draw the two-line small print. Called after every modal step so the dialog's
   own update does not leave it blank. */
static void DrawSettingsNote(DialogPtr dialog)
{
    GrafPtr savePort;
    PenState savePen;
    RGBColor saveFore;
    short saveFont, saveSize, saveFace;
    RGBColor noteGrey = {0x8888, 0x8888, 0x8888};

    GetPort(&savePort);
    SetPort(dialog);
    GetPenState(&savePen);
    GetForeColor(&saveFore);
    saveFont = dialog->txFont;
    saveSize = dialog->txSize;
    saveFace = dialog->txFace;

    TextFont(kFontGeneva);
    TextSize(kSettingsNoteSize);
    TextFace(normal);
    RGBForeColor(&noteGrey);

    MoveTo(kSettingsNoteLeft, kSettingsNoteBaseline1);
    DrawString("\phttps only. Certificate checking stops at Google Trust Services;");
    MoveTo(kSettingsNoteLeft, kSettingsNoteBaseline2);
    DrawString("\pother hosts need it turned off.");

    SetPenState(&savePen);
    RGBForeColor(&saveFore);
    TextFont(saveFont);
    TextSize(saveSize);
    TextFace(saveFace);
    SetPort(savePort);
}

/* Copy a C string into a bounded buffer. */
static void CopyCString(char *out, short max, const char *text)
{
    short len = 0;

    if (out == NULL || max <= 0) {
        return;
    }

    while (text != NULL && text[len] != '\0' && len < (short)(max - 1)) {
        out[len] = text[len];
        len++;
    }
    out[len] = '\0';
}

/* Replace the text of an editable dialog item.
 *
 * An EditText item's handle is a TEHandle, not a text handle: SetDialogItemText
 * on it writes a Pascal string over the TERec header and corrupts the heap.
 * TextEdit owns that text, so it has to be set through TextEdit. */
static void SetItemCString(DialogPtr dialog, short item, const char *text)
{
    Rect itemRect;
    Handle itemHandle;
    short itemType;
    Str255 p;

    GetDialogItem(dialog, item, &itemType, &itemHandle, &itemRect);

    if ((itemType & editText) != 0 && itemHandle != NULL) {
        TEHandle te = (TEHandle)itemHandle;
        short len = 0;

        if (text != NULL) {
            while (text[len] != '\0' && len < 255) {
                len++;
            }
        }

        TESetText((Ptr)text, len, te);
        TEUpdate(&itemRect, te);
        return;
    }

    CStrToPStr(text != NULL ? text : "", p);
    SetDialogItemText(itemHandle, p);
}

/* Read an editable dialog item into a bounded C string */
static void GetItemCString(DialogPtr dialog, short item, char *out, short outSize)
{
    Rect itemRect;
    Handle itemHandle;
    short itemType;
    Str255 p;
    short len;

    if (out == NULL || outSize <= 0) {
        return;
    }

    out[0] = '\0';

    GetDialogItem(dialog, item, &itemType, &itemHandle, &itemRect);

    if ((itemType & editText) != 0 && itemHandle != NULL) {
        TEHandle te = (TEHandle)itemHandle;

        if (te == NULL || *te == NULL || (*te)->hText == NULL) {
            return;
        }

        len = (*te)->teLength;
        if (len > (short)(outSize - 1)) {
            len = (short)(outSize - 1);
        }
        if (len > 0) {
            BlockMoveData(*(*te)->hText, out, (Size)len);
        }
        out[len] = '\0';
        return;
    }

    GetDialogItemText(itemHandle, p);

    len = p[0];
    if (len > (short)(outSize - 1)) {
        len = (short)(outSize - 1);
    }

    memcpy(out, p + 1, (size_t)len);
    out[len] = '\0';
}

/* Show the modal settings dialog and persist the result */
static void ShowSettingsDialog(void)
{
    DialogPtr dialog;
    GrafPtr savePort;
    char value[128];
    char originalModel[128];
    short item;
    Boolean accepted = false;

    GetPort(&savePort);

    dialog = GetNewDialog(kSettingsDialogID, NULL, (WindowPtr)-1);
    if (dialog == NULL) {
        HandleError(kErrResourceNotFound, 0, false);
        SetPort(savePort);
        return;
    }

    SetPort(dialog);

    ParamText("\pProvider:", "\pModel:", "\pAPI key:",
              "\pBase URL (empty = provider default):");

    SetItemCString(dialog, kSettingsProviderEdit, API_ProviderName(API_GetProvider()));
    SetItemCString(dialog, kSettingsModelEdit, API_GetModel());
    SetItemCString(dialog, kSettingsKeyEdit, API_GetAPIKey());
    SetItemCString(dialog, kSettingsURLEdit, API_GetBaseURL());

    /* What the field started with decides whether the user edited the model */
    GetItemCString(dialog, kSettingsModelEdit, originalModel, (short)sizeof(originalModel));

    ShowWindow(dialog);
    SetDialogDefaultItem(dialog, kSettingsOK);
    SetDialogCancelItem(dialog, kSettingsCancel);

    for (;;) {
        ModalDialog(NULL, &item);

        /* The dialog's own update erases user items, so the small print is
           painted after each step rather than once. */
        DrawSettingsNote(dialog);

        if (item == kSettingsOK) {
            accepted = true;
            break;
        }
        if (item == kSettingsCancel) {
            break;
        }
    }

    if (accepted) {
        APIProvider provider;

        GetItemCString(dialog, kSettingsProviderEdit, value, (short)sizeof(value));
        if (API_ProviderFromName(value, &provider)) {
            API_SetProvider(provider);
        }
        /* Anything else keeps the previous provider */

        /* Switching provider can adopt that provider's default model. Only a
           model the user really edited may override it. */
        GetItemCString(dialog, kSettingsModelEdit, value, (short)sizeof(value));
        if (value[0] == '\0' || strcmp(value, originalModel) == 0) {
            CopyCString(value, (short)sizeof(value), API_GetModel());
        }
        API_SetModel(value);

        /* Leave the field showing the model that will actually be used */
        if (strcmp(value, originalModel) != 0) {
            Rect modelRect;
            Handle modelHandle;
            short modelType;

            SetItemCString(dialog, kSettingsModelEdit, value);
            GetDialogItem(dialog, kSettingsModelEdit, &modelType, &modelHandle, &modelRect);
            InvalRect(&modelRect);
        }

        GetItemCString(dialog, kSettingsKeyEdit, value, (short)sizeof(value));
        API_SetAPIKey(value);

        GetItemCString(dialog, kSettingsURLEdit, value, (short)sizeof(value));
        API_SetBaseURL(value);
    }

    DisposeDialog(dialog);
    SetPort(savePort);
}

/* Update the menu state based on current application mode */
void UpdateMenus(void)
{
    MenuRef fileMenu   = GetMenu(kMenuFile);
    MenuRef modelsMenu = GetMenu(kMenuModels);
    WindowRef w        = FrontWindow();

    if (fileMenu == NULL || modelsMenu == NULL) {
        return;
    }

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
        CheckItem(modelsMenu, kItemRemoteModel, gActiveAIModel == kRemoteModel);
        CheckItem(modelsMenu, kItemTemplateModel, gActiveAIModel == kTemplateModel);

        break;
    }

    /* Menu state changes must be visible immediately, not at next update */
    DrawMenuBar();
}

/* Leave the chat window and return to the splash screen */
static void ReturnToSplash(void)
{
    WindowManager_CloseWindow(kWindowTypeChat);

    if (!WindowManager_OpenWindow(kWindowTypeSplash)) {
        HandleError(kErrWindowCreation, kCtxCreatingMainWindow, true);
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
            AboutWindow_Show();
        else {
            GetMenuItemText(GetMenu(kMenuApple), menuItem, str);
            OpenDeskAcc(str);
        }
    }
    else if (menuID == kMenuFile) {
        switch (menuItem) {
        case kItemChat:
            /* Switch to chat mode only if the window really came up */
            if (gAppMode == kModeMainSplash || !WindowManager_IsWindowVisible(kWindowTypeChat)) {
                if (!WindowManager_OpenWindow(kWindowTypeChat)) {
                    HandleError(kErrWindowCreation, kCtxOpeningChatWindow, false);
                    break;
                }
                WindowManager_CloseWindow(kWindowTypeSplash);
            }
            else {
                /* Already in chat mode, bring window to front */
                w = WindowManager_GetWindowRef(kWindowTypeChat);
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
                    ReturnToSplash();
                }
                else if (w == WindowManager_GetWindowRef(kWindowTypeSplash)) {
                    /* Closing main window quits the application */
                    API_Shutdown();
                    QuitApplication(false);
                }
                else {
                    /* Other window */
                    DisposeWindow(w);
                }
            }
            break;

        case kItemSettings:
            ShowSettingsDialog();
            break;

        case kItemQuit:
            /* Stop the network client before the process goes away */
            API_Shutdown();
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

                case kItemRemoteModel:
                    /* Set to the remote AI model */
                    SetActiveAIModel(kRemoteModel);
                    ChatWindow_AddMessage("Switched to remote AI model.", false);
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
