#ifndef ERROR_H
#define ERROR_H

#include <Types.h>

/* Error handling functions */
void HandleError(short errorCode, short contextID, Boolean fatal);
OSErr CheckMemory(void);

/* Standard function for exiting the application */
void QuitApplication(Boolean showError);

#endif /* ERROR_H */