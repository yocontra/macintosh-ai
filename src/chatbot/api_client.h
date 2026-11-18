#ifndef API_CLIENT_H
#define API_CLIENT_H
#include <Types.h>

/* The implementation is C++ (it drives the C++ net68k client) while every
   caller is C, so the declarations need C linkage.  Signatures are unchanged. */
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    kAPIStateIdle = 0, kAPIStateConnecting, kAPIStateStreaming,
    kAPIStateDone, kAPIStateError
} APIState;

typedef enum { kProviderAnthropic = 0, kProviderOpenAI = 1 } APIProvider;

void        API_Init(void);
void        API_Shutdown(void);                      /* before ExitToShell() */
Boolean     API_BeginRequest(void);                  /* false + API_LastError() if no key or busy */
short       API_Poll(char *out, short outMax);       /* newly received assistant bytes, NUL-terminated */
APIState    API_State(void);
void        API_Cancel(void);
const char *API_LastError(void);

APIProvider  API_GetProvider(void);  void API_SetProvider(APIProvider p);
const char  *API_GetModel(void);     void API_SetModel(const char *m);
const char  *API_GetAPIKey(void);    void API_SetAPIKey(const char *k);
const char  *API_GetBaseURL(void);   void API_SetBaseURL(const char *u); /* "" = provider default */

#ifdef __cplusplus
}
#endif

#endif
