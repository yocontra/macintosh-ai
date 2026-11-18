#ifndef API_PARSE_H
#define API_PARSE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Toolbox-free SSE decoder for the supported providers.
 *
 * The dialect ids are deliberately the same numbers api_client.h uses for
 * APIProvider (0 = Anthropic, 1 = OpenAI) so the client can pass its provider
 * through unchanged; Gemini is an extra dialect the client may select.
 *
 * The decoder keeps its own line accumulator, so a `data:` line split across
 * two HTTP body chunks is decoded correctly.  Decoded assistant text is
 * appended to `buf` (always NUL-terminated, `cap` counts the NUL). */

enum {
    kAPIDialectAnthropic = 0,
    kAPIDialectOpenAI    = 1,
    kAPIDialectGemini    = 2
};

typedef struct { char *buf; short len; short cap; } APIParse;

void  APIParse_Reset(APIParse *p, char *buf, short cap);
short APIParse_Feed(APIParse *p, const char *data, short len, int provider);
int   APIParse_Done(int provider);

#ifdef __cplusplus
}
#endif

#endif /* API_PARSE_H */
