#ifndef PREFS_H
#define PREFS_H
#include <Types.h>

/* prefs.c is C and api_client.cpp (C++) calls it: the declarations need C
   linkage.  Signatures are unchanged. */
#ifdef __cplusplus
extern "C" {
#endif

Boolean     Prefs_Load(void);                            /* once at startup */
Boolean     Prefs_Save(void);
const char *Prefs_Get(const char *key, const char *fallback);
void        Prefs_Set(const char *key, const char *value); /* values truncated to 255 */

#ifdef __cplusplus
}
#endif

#endif
