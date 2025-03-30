#include <Windows.h>

#include "../constants.h"
#include "chat_window.h"
#include "splash_window.h"
#include "window_manager.h"

/*
 * Window Manager module that coordinates operations between
 * individual window modules. Provides high-level window management
 * functions that delegate to the specific module implementations.
 */

/* Window management functions */

void SetupMainWindow(void)
{
    SplashWindow_Initialize();
}

void OpenChatWindow(void)
{
    ChatWindow_Initialize();
    ChatWindow_Show(true);
    SplashWindow_Show(false);
    gAppMode = kModeChatWindow;
}

void CloseChatWindow(void)
{
    ChatWindow_Show(false);
    SplashWindow_Show(true);
    gAppMode = kModeMainSplash;
}

void DoUpdate(WindowRef window)
{
    if (window == SplashWindow_GetWindowRef()) {
        SplashWindow_Update();
    }
    else if (window == ChatWindow_GetWindowRef()) {
        ChatWindow_Update();
    }
}

void HandleReturnInChatWindow(void)
{
    ChatWindow_ProcessReturnKey();
}