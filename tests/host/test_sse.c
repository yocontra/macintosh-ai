/*
 * test_sse.c -- host-side test for the Toolbox-free SSE decoder.
 *
 * Exercises exactly the contract in src/chatbot/api_parse.h:
 *
 *   APIParse_Reset(&p, buf, cap);
 *   APIParse_Feed(&p, data, len, provider);   -> bytes appended to p->buf
 *   APIParse_Done(provider);                  -> non-zero after the terminal event
 *
 * Cases (plan Verification item 3):
 *   1. Anthropic stream "Hel" / "lo" / " world" + message_stop
 *      -> exactly "Hello world", APIParse_Done() != 0.
 *   2. OpenAI stream with the same deltas + [DONE]
 *      -> exactly "Hello world", APIParse_Done() != 0.
 *   3. Truncated stream (no terminal event)
 *      -> partial text "Hello world", APIParse_Done() == 0.
 *
 * The test is plain C. It does not include <Types.h>, Toolbox headers or any
 * net68k header; only api_parse.h and the C standard library.
 */

#include <stdio.h>
#include <string.h>

#include "api_parse.h"

/* Provider values match APIProvider in api_client.h (which pulls in <Types.h>
 * and therefore cannot be included by a host test). */
#define PROVIDER_ANTHROPIC 0
#define PROVIDER_OPENAI 1

#define TEXT_BUFFER_SIZE 4096

static int gFailures = 0;

static void fail(const char *context, const char *detail)
{
    fprintf(stderr, "FAIL: %s: %s\n", context, detail);
    gFailures++;
}

/* p->buf is not required to be NUL-terminated; compare p->len bytes. */
static void expect_text(const APIParse *p, const char *want, const char *context)
{
    size_t wantLen = strlen(want);

    if ((size_t)p->len != wantLen || memcmp(p->buf, want, wantLen) != 0) {
        fprintf(stderr, "FAIL: %s: got \"%.*s\" (%d bytes), want \"%s\" (%u bytes)\n", context,
                (int)p->len, p->buf, (int)p->len, want, (unsigned)wantLen);
        gFailures++;
    }
}

static void expect_done(int provider, int wantDone, const char *context)
{
    int done = APIParse_Done(provider) ? 1 : 0;

    if (done != wantDone) {
        fprintf(stderr, "FAIL: %s: APIParse_Done() == %d, want %d\n", context, done, wantDone);
        gFailures++;
    }
}

/*
 * Feed the first feedCount chunks (an event at a time, as the network stack
 * delivers them) and check the accumulated text and the terminal flag.
 */
static void check_stream(int provider, const char *const *chunks, int feedCount,
                         const char *wantText, int wantDone, const char *context)
{
    static char buffer[TEXT_BUFFER_SIZE];
    APIParse parser;
    int i;

    memset(buffer, 0, sizeof buffer);
    APIParse_Reset(&parser, buffer, (short)sizeof buffer);

    for (i = 0; i < feedCount; i++) {
        short appended = APIParse_Feed(&parser, chunks[i], (short)strlen(chunks[i]), provider);
        if (appended < 0) {
            fail(context, "APIParse_Feed() returned a negative byte count");
            return;
        }
    }

    expect_text(&parser, wantText, context);
    expect_done(provider, wantDone, context);

    /* Reset must return the parser to an empty state. */
    APIParse_Reset(&parser, buffer, (short)sizeof buffer);
    if (parser.len != 0) {
        fail(context, "APIParse_Reset() did not clear len");
    }
}

/* ----------------------------- Anthropic ----------------------------- */

static const char *const kAnthropicChunks[] = {
    "event: message_start\n"
    "data: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_test\",\"type\":\"message\","
    "\"role\":\"assistant\",\"model\":\"claude-sonnet-4-5\",\"content\":[],\"stop_reason\":null,"
    "\"usage\":{\"input_tokens\":9,\"output_tokens\":1}}}\n\n",

    "event: content_block_start\n"
    "data: {\"type\":\"content_block_start\",\"index\":0,"
    "\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\n\n",

    "event: ping\n"
    "data: {\"type\":\"ping\"}\n\n",

    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,"
    "\"delta\":{\"type\":\"text_delta\",\"text\":\"Hel\"}}\n\n",

    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,"
    "\"delta\":{\"type\":\"text_delta\",\"text\":\"lo\"}}\n\n",

    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,"
    "\"delta\":{\"type\":\"text_delta\",\"text\":\" world\"}}\n\n",

    "event: content_block_stop\n"
    "data: {\"type\":\"content_block_stop\",\"index\":0}\n\n",

    "event: message_delta\n"
    "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"},"
    "\"usage\":{\"output_tokens\":3}}\n\n",

    "event: message_stop\n"
    "data: {\"type\":\"message_stop\"}\n\n"
};

#define ANTHROPIC_CHUNK_COUNT ((int)(sizeof kAnthropicChunks / sizeof kAnthropicChunks[0]))

/* ------------------------------- OpenAI ------------------------------- */

static const char *const kOpenAIChunks[] = {
    "data: {\"id\":\"chatcmpl-test\",\"object\":\"chat.completion.chunk\",\"created\":1,"
    "\"model\":\"gpt-test\",\"choices\":[{\"index\":0,\"delta\":{\"role\":\"assistant\"},"
    "\"finish_reason\":null}]}\n\n",

    "data: {\"id\":\"chatcmpl-test\",\"object\":\"chat.completion.chunk\",\"created\":1,"
    "\"model\":\"gpt-test\",\"choices\":[{\"index\":0,\"delta\":{\"content\":\"Hel\"},"
    "\"finish_reason\":null}]}\n\n",

    "data: {\"id\":\"chatcmpl-test\",\"object\":\"chat.completion.chunk\",\"created\":1,"
    "\"model\":\"gpt-test\",\"choices\":[{\"index\":0,\"delta\":{\"content\":\"lo\"},"
    "\"finish_reason\":null}]}\n\n",

    "data: {\"id\":\"chatcmpl-test\",\"object\":\"chat.completion.chunk\",\"created\":1,"
    "\"model\":\"gpt-test\",\"choices\":[{\"index\":0,\"delta\":{\"content\":\" world\"},"
    "\"finish_reason\":null}]}\n\n",

    ("data: {\"id\":\"chatcmpl-test\",\"object\":\"chat.completion.chunk\",\"created\":1,"
     "\"model\":\"gpt-test\",\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"),

    "data: [DONE]\n\n"
};

#define OPENAI_CHUNK_COUNT ((int)(sizeof kOpenAIChunks / sizeof kOpenAIChunks[0]))

/* --------------------------- Feed semantics --------------------------- */

static void test_feed_return_value(void)
{
    static char buffer[TEXT_BUFFER_SIZE];
    APIParse parser;
    short appended;

    APIParse_Reset(&parser, buffer, (short)sizeof buffer);
    appended = APIParse_Feed(&parser, kAnthropicChunks[3], (short)strlen(kAnthropicChunks[3]),
                             PROVIDER_ANTHROPIC);
    if (appended != 3) {
        fprintf(stderr, "FAIL: APIParse_Feed returned %d for a 3-byte delta, want 3\n", (int)appended);
        gFailures++;
    }
    expect_text(&parser, "Hel", "feed return value");
}

int main(void)
{
    /* Complete streams. */
    check_stream(PROVIDER_ANTHROPIC, kAnthropicChunks, ANTHROPIC_CHUNK_COUNT, "Hello world", 1,
                 "anthropic complete stream");
    check_stream(PROVIDER_OPENAI, kOpenAIChunks, OPENAI_CHUNK_COUNT, "Hello world", 1,
                 "openai complete stream");

    /* Truncated streams: everything but the terminal event was received. */
    check_stream(PROVIDER_ANTHROPIC, kAnthropicChunks, ANTHROPIC_CHUNK_COUNT - 1, "Hello world", 0,
                 "anthropic truncated stream");
    check_stream(PROVIDER_OPENAI, kOpenAIChunks, OPENAI_CHUNK_COUNT - 1, "Hello world", 0,
                 "openai truncated stream");

    /* APIParse_Feed returns the number of bytes appended. */
    test_feed_return_value();

    if (gFailures != 0) {
        fprintf(stderr, "%d assertion(s) failed\n", gFailures);
        return 1;
    }

    printf("test_sse: all assertions passed\n");
    return 0;
}
