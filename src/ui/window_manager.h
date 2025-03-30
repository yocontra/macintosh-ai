#ifndef WINDOW_MANAGER_H
#define WINDOW_MANAGER_H

#include <Windows.h>

/*
 * Window manager module that coordinates between individual window implementations.
 * These functions provide high-level window management operations that
 * delegate to the specific window module implementations.
 */

/* Window management functions */
void SetupMainWindow(void);
void OpenChatWindow(void);
void CloseChatWindow(void);
void DoUpdate(WindowRef window);
void HandleReturnInChatWindow(void);

#endif /* WINDOW_MANAGER_H */