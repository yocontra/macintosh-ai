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

/* kProviderOpenRouter and kProviderCompatible speak the OpenAI dialect;
   kProviderGemini speaks the Google dialect.  The values are written to the
   `provider` pref by name, so they only need to stay stable for the enum. */
typedef enum {
    kProviderAnthropic = 0,
    kProviderOpenAI = 1,
    kProviderGemini = 2,
    kProviderOpenRouter = 3,
    kProviderCompatible = 4
} APIProvider;

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

/* Provider names as written in Settings and in the `provider` pref.  The
   mapping is case-insensitive and ignores surrounding spaces; "openai-
   compatible" is accepted as an alias for "compatible".  API_ProviderName
   never returns NULL (an out-of-range value maps to "anthropic"). */
const char  *API_ProviderName(APIProvider p);
Boolean      API_ProviderFromName(const char *name, APIProvider *out);

/* TLS certificate verification (pref "tls_verify", default true), forwarded
   to the net68k engine (net68k_set_verify_peer).  Off accepts any
   certificate; the app still reports it in API_GetStatusText. */
Boolean      API_GetCertificateCheck(void);
void         API_SetCertificateCheck(Boolean on);

/* One line for the chat window header: "Markov chain",
   "Remote AI · no API key", "Remote AI · TLS: no cert check",
   "Remote AI · <model>".  Never longer than max-1 and never longer than 45
   bytes, because the header strip is narrow.  The separator is MacRoman 0xE1
   (middle dot). */
void         API_GetStatusText(char *out, short max);

#ifdef __cplusplus
}
#endif

#endif
