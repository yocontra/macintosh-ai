/* api_parse.cpp — SSE / JSON delta decoder.
 *
 * Toolbox-free on purpose: the host unit tests compile this file natively
 * (tests/host), so it includes <string.h> and gason.hpp and nothing else.
 *
 * Wire format handled:
 *   Anthropic  "data: {\"type\":\"content_block_delta\",\"delta\":{\"text\":\"..\"}}"
 *              "event: message_stop" / {"type":"message_stop"}
 *   OpenAI     "data: {\"choices\":[{\"delta\":{\"content\":\"..\"}}]}"
 *              "data: [DONE]"
 *
 * SSE lines can be split across body chunks, so complete lines are only
 * parsed once the terminating '\n' has arrived.  A line longer than the
 * accumulator (a single delta larger than the buffer) is dropped rather than
 * truncated, which keeps the JSON parse honest.
 */
#include "api_parse.h"

#include <string.h>

#include "gason.hpp"

/* Mirror of APIProvider; api_client.h cannot be included (it needs <Types.h>). */
enum { kParseAnthropic = 0, kParseOpenAI = 1 };

#define kLineMax 1024

static char  sLine[kLineMax];
static short sLineLen;
static int   sSkipLine;          /* line overflowed: drop it up to the next '\n' */
static unsigned char sDoneMask;  /* bit per provider, set by its end marker */

static const char *JsonString(gason::JsonValue v)
{
    bool ok = false;
    const char *s = v.toString(&ok);
    return ok ? s : 0;
}

static short AppendText(APIParse *p, const char *s, int n)
{
    int room;

    if (n <= 0 || p->cap <= 0)
        return 0;
    room = (int)p->cap - 1 - (int)p->len;
    if (room <= 0)
        return 0;
    if (n > room)
        n = room;
    memcpy(p->buf + p->len, s, (size_t)n);
    p->len = (short)(p->len + n);
    p->buf[p->len] = '\0';
    return (short)n;
}

/* Decode one complete, CR-stripped line.  `line` is writable: gason unescapes
   JSON strings in place. */
static short ParseLine(APIParse *p, char *line, short len, int provider)
{
    char *json;
    gason::JsonValue root;
    gason::JsonAllocator alloc;
    gason::JsonValue delta;
    const char *type;
    const char *text;

    if (len <= 0 || line[0] == ':')      /* blank line or SSE comment */
        return 0;

    if (memcmp(line, "event:", 6) == 0) {
        char *v = line + 6;
        while (*v == ' ')
            v++;
        if (provider == kParseAnthropic && strcmp(v, "message_stop") == 0)
            sDoneMask |= (unsigned char)(1u << kParseAnthropic);
        return 0;
    }

    if (memcmp(line, "data:", 5) != 0)
        return 0;

    json = line + 5;
    while (*json == ' ')
        json++;

    if (strcmp(json, "[DONE]") == 0) {
        sDoneMask |= (unsigned char)(1u << provider);
        return 0;
    }
    if (!*json)
        return 0;
    if (gason::jsonParse(json, root, alloc) != gason::JSON_PARSE_OK)
        return 0;

    if (provider == kParseAnthropic) {
        type = JsonString(root("type"));
        if (type && strcmp(type, "message_stop") == 0) {
            sDoneMask |= (unsigned char)(1u << kParseAnthropic);
            return 0;
        }
        if (!type || strcmp(type, "content_block_delta") != 0)
            return 0;
        delta = root("delta");
        text = JsonString(delta("text"));
    } else {
        gason::JsonValue choices = root("choices");
        if (!choices.isArray())
            return 0;
        delta = choices.at(0);
        if (!delta.isObject())
            return 0;
        delta = delta("delta");
        text = JsonString(delta("content"));
    }

    if (!text)
        return 0;
    return AppendText(p, text, (int)strlen(text));
}

void APIParse_Reset(APIParse *p, char *buf, short cap)
{
    if (p) {
        p->buf = buf;
        p->len = 0;
        p->cap = cap;
        if (buf && cap > 0)
            buf[0] = '\0';
    }
    sLineLen = 0;
    sSkipLine = 0;
    sDoneMask = 0;
}

short APIParse_Feed(APIParse *p, const char *data, short len, int provider)
{
    short i;
    short appended = 0;

    if (!p || !p->buf || !data || len <= 0)
        return 0;

    for (i = 0; i < len; i++) {
        char c = data[i];

        if (c == '\n') {
            if (sSkipLine) {
                sSkipLine = 0;
                sLineLen = 0;
                continue;
            }
            if (sLineLen > 0 && sLine[sLineLen - 1] == '\r')
                sLineLen--;
            sLine[sLineLen] = '\0';
            appended = (short)(appended + ParseLine(p, sLine, sLineLen, provider));
            sLineLen = 0;
            continue;
        }

        if (sSkipLine)
            continue;
        if (sLineLen >= (short)(kLineMax - 1)) {
            sSkipLine = 1;      /* overlong line: discard, keep stream in sync */
            sLineLen = 0;
            continue;
        }
        sLine[sLineLen++] = c;
    }

    return appended;
}

int APIParse_Done(int provider)
{
    if (provider < 0 || provider > 1)
        return 0;
    return (sDoneMask & (1u << provider)) != 0;
}
