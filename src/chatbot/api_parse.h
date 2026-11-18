#ifndef API_PARSE_H
#define API_PARSE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Toolbox-free SSE decoder for the two supported providers.
 *
 * Provider ids mirror APIProvider in api_client.h (which pulls in <Types.h>
 * and therefore cannot be included here):
 *     0 = Anthropic, 1 = OpenAI
 *
 * The decoder keeps its own line accumulator, so a `data:` line split across
 * two HTTP body chunks is decoded correctly.  Decoded assistant text is
 * appended to `buf` (always NUL-terminated, `cap` counts the NUL). */

typedef struct { char *buf; short len; short cap; } APIParse;

void  APIParse_Reset(APIParse *p, char *buf, short cap);
short APIParse_Feed(APIParse *p, const char *data, short len, int provider);
int   APIParse_Done(int provider);

#ifdef __cplusplus
}
#endif

#endif /* API_PARSE_H */
