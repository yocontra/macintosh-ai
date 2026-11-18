/* api_client.cpp — non-blocking bridge between the chat window and net68k.
 *
 * The engine (libs/net68k/net/HttpClient) is a cooperative state machine over
 * Open Transport: every socket read/write calls MacYield, which calls
 * SystemTask(), so the Mac stays alive while a transfer runs.  It has no
 * resumable "poll one step" entry point, so the split is:
 *
 *   API_Init()          — load prefs; the TLS contexts are built on first use.
 *   API_BeginRequest()  — validate, build the JSON body, and open the TLS
 *                         connection (HttpClient::WarmUp).  No POST yet.
 *   API_Poll()          — first call issues the streaming POST and collects
 *                         the decoded text; every call then hands the caller
 *                         up to outMax bytes of text that it has not seen.
 *
 * Nothing here touches the Toolbox, so it is safe to call from the main loop.
 */
#include <string.h>
#include <stdio.h>

#include "api_client.h"
#include "api_parse.h"
#include "../prefs.h"
#include "markov.h"        /* gConversationHistory, ConversationMessage */
#include "model_manager.h"
#include "HttpClient.h"

/* ── Engine logging hooks ─────────────────────────────────────────────
   The vendored engine declares these callbacks (DebugLog has C++ linkage,
   the two declared from .c files have C linkage) and calls them at nearly
   every step of a request.  macintosh-ai has no debug console, so all three
   are no-ops. */
void DebugLog(const char* func, const char* msg) { (void)func; (void)msg; }
extern "C" void uecc_con_log(const char* tag, const char* msg) { (void)tag; (void)msg; }
extern "C" void fast_alloc_log(const char* func, const char* msg) { (void)func; (void)msg; }

/* ── Storage ─────────────────────────────────────────────────────────
   One global client: its constructor runs before main(), which is where the
   engine wants to call SetApplLimit/MaxApplZone.  All buffers are static —
   no per-request heap traffic except the std::string copies inside the
   engine itself. */
static HttpClient sHttp;

static APIState     sState = kAPIStateIdle;
static APIProvider  sProvider = kProviderAnthropic;

static char sModel[64]    = "";
static char sKey[128]     = "";
static char sBaseURL[128] = "";
static char sError[128];

/* Certificate verification lives in the engine: the pref only seeds it. */
extern "C" void net68k_set_verify_peer(int on);
extern "C" int  net68k_get_verify_peer(void);

/* sURL holds "https://" + host[:port] + base path + dialect path; the Gemini
   path embeds a 63-byte model name, so the old 192 bytes were not enough. */
static char sURL[384];
static char sHost[160];       /* host plus ":port" when the port is not 443 */
static char sHeaders[320];    /* OpenRouter sends three header lines */
static char sBody[8192];

/* Decoded assistant text for the current request, plus the read cursor.
   Same bound as the chat window's display buffer. */
static char      sText[kMaxResponseLength];
static APIParse  sParse;
static short     sTextSent;

/* Transfer bookkeeping. */
static int sFinished;   /* PostStream returned                    */
static int sCompleted;  /* onComplete ran (false on TLS failures) */
static int sFailed;
static int sCancelled;

/* ── Small helpers ───────────────────────────────────────────────── */
static Boolean StartsWithCI(const char* s, const char* prefix)
{
    for (; *prefix; s++, prefix++) {
        char a = *s, b = *prefix;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b)
            return false;
    }
    return true;
}

static void CopyBounded(char* dst, short cap, const char* src)
{
    if (!src)
        src = "";
    strncpy(dst, src, (size_t)cap - 1);
    dst[cap - 1] = '\0';
}

static void SetError(const char* msg)
{
    CopyBounded(sError, (short)sizeof(sError), msg && *msg ? msg : "unknown error");
}

/* Rewrite the current error as "<provider>: <existing text>". */
static void PrefixError(const char* name)
{
    char tmp[128];

    CopyBounded(tmp, (short)sizeof(tmp), sError);
    snprintf(sError, sizeof sError, "%s: %s", name, tmp);
}

static Boolean EqualsCI(const char* s, const char* name)
{
    for (; *s && *name; s++, name++) {
        char a = *s, b = *name;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b)
            return false;
    }
    return *s == '\0' && *name == '\0';
}

/* Copy `src`, dropping leading and trailing spaces (the Settings field is
   free text, so " Gemini " has to work). */
static void CopyTrimmed(char* dst, short cap, const char* src)
{
    const char* end;
    short n;

    if (!src)
        src = "";
    while (*src == ' ' || *src == '\t')
        src++;
    end = src + strlen(src);
    while (end > src && (end[-1] == ' ' || end[-1] == '\t'))
        end--;
    n = (short)(end - src);
    if (n > cap - 1)
        n = (short)(cap - 1);
    memcpy(dst, src, (size_t)n);
    dst[n] = '\0';
}

/* ── Provider table ──────────────────────────────────────────────────
   One row per APIProvider value.  `usesOpenAIDialect` covers openai,
   openrouter and compatible; those three differ only in default base URL,
   auth header spelling and whether extra headers are sent. */
typedef struct {
    const char* name;
    const char* defaultBase;
    const char* defaultModel;
    Boolean     usesOpenAIDialect;
    Boolean     usesGeminiDialect;
} ProviderInfo;

static const ProviderInfo kProviders[] = {
    { "anthropic",  "https://api.anthropic.com",                 "claude-sonnet-4-5",           false, false },
    { "openai",     "https://api.openai.com",                    "gpt-4o-mini",                 true,  false },
    { "gemini",     "https://generativelanguage.googleapis.com", "gemini-2.0-flash",            false, true  },
    { "openrouter", "https://openrouter.ai/api",                 "anthropic/claude-sonnet-4.5", true,  false },
    { "compatible", "",                                           "",                            true,  false }
};

#define kProviderCount ((short)(sizeof(kProviders) / sizeof(kProviders[0])))

static const ProviderInfo* ProviderInfoFor(APIProvider p)
{
    unsigned int i = (unsigned int)p;

    return (i < (unsigned int)kProviderCount) ? &kProviders[i] : &kProviders[0];
}

static const char* CurrentProviderName(void)
{
    return ProviderInfoFor(sProvider)->name;
}

const char* API_ProviderName(APIProvider p)
{
    return ProviderInfoFor(p)->name;
}

Boolean API_ProviderFromName(const char* name, APIProvider* out)
{
    char tmp[24];
    short i;

    if (!name || !out)
        return false;
    CopyTrimmed(tmp, (short)sizeof(tmp), name);
    for (i = 0; i < kProviderCount; i++) {
        if (EqualsCI(tmp, kProviders[i].name)) {
            *out = (APIProvider)i;
            return true;
        }
    }
    if (EqualsCI(tmp, "openai-compatible")) {
        *out = kProviderCompatible;
        return true;
    }
    return false;
}

/* True when `m` is one of the table's default model names (the empty string
   counts when it was the compatible provider's default). */
static Boolean IsBuiltInDefaultModel(const char* m)
{
    short i;

    if (!m || !*m)
        return true;
    for (i = 0; i < kProviderCount; i++) {
        if (kProviders[i].defaultModel[0] && strcmp(m, kProviders[i].defaultModel) == 0)
            return true;
    }
    return false;
}

static Boolean IsTlsFailure(const char* msg)
{
    if (!msg || !*msg)
        return false;
    return strstr(msg, "-9984") != NULL ||
           strstr(msg, "ertificate") != NULL ||     /* Certificate/certificate */
           strstr(msg, "X509") != NULL ||
           strstr(msg, "x509") != NULL;
}

/* Turn an engine error into something a user can act on. */
static void SetEngineError(const char* msg, const char* fallback)
{
    if (IsTlsFailure(msg)) {
        snprintf(sError, sizeof sError, "%s: TLS: certificate not trusted",
                 CurrentProviderName());
        return;
    }
    if (msg && *msg) {
        char tmp[96];
        CopyBounded(tmp, (short)sizeof(tmp), msg);
        SetError(tmp);
        return;
    }
    SetError(fallback);
}

static void SetHttpError(unsigned int status)
{
    if (status == 401 || status == 403)
        snprintf(sError, sizeof sError, "%s: API key rejected - check Settings",
                 CurrentProviderName());
    else if (status == 429)
        snprintf(sError, sizeof sError, "%s: rate limited by the provider",
                 CurrentProviderName());
    else if (status >= 500)
        snprintf(sError, sizeof sError, "%s: provider server error",
                 CurrentProviderName());
    else
        snprintf(sError, sizeof sError, "%s: HTTP %u", CurrentProviderName(), status);
}

/* ── Request body ────────────────────────────────────────────────── */
/* MacRoman 0x80..0xFF -> Unicode, so accented text reaches the API as valid
   UTF-8 instead of raw MacRoman bytes. */
static const unsigned short kMacRomanHigh[128] = {
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1,
    0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,
    0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3,
    0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,
    0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF,
    0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
    0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211,
    0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,
    0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB,
    0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
    0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA,
    0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
    0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1,
    0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
    0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC,
    0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7
};

static int AppendRaw(int n, const char* s)
{
    int cap = (int)sizeof(sBody);
    while (*s && n < cap - 1)
        sBody[n++] = *s++;
    sBody[n] = '\0';
    return n;
}

/* JSON-escape `src` into sBody at n, never past `limit` and never splitting an
   escape sequence.  Line ends (CR and LF) become \n. */
static int AppendEscapedJSON(int n, const char* src, int limit)
{
    static const char kHex[] = "0123456789abcdef";

    for (; *src; src++) {
        unsigned char c = (unsigned char)*src;
        char tmp[8];
        int t = 0;

        if (c == '"' || c == '\\') {
            tmp[t++] = '\\';
            tmp[t++] = (char)c;
        } else if (c == '\n' || c == '\r') {
            tmp[t++] = '\\';
            tmp[t++] = 'n';
        } else if (c == '\t') {
            tmp[t++] = '\\';
            tmp[t++] = 't';
        } else if (c < 0x20) {
            tmp[t++] = '\\'; tmp[t++] = 'u'; tmp[t++] = '0'; tmp[t++] = '0';
            tmp[t++] = kHex[c >> 4]; tmp[t++] = kHex[c & 0x0F];
        } else if (c < 0x80) {
            tmp[t++] = (char)c;
        } else {
            unsigned int cp = kMacRomanHigh[c - 0x80];
            if (cp < 0x800) {
                tmp[t++] = (char)(0xC0 | (cp >> 6));
                tmp[t++] = (char)(0x80 | (cp & 0x3F));
            } else {
                tmp[t++] = (char)(0xE0 | (cp >> 12));
                tmp[t++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                tmp[t++] = (char)(0x80 | (cp & 0x3F));
            }
        }

        if (n + t > limit)
            break;
        memcpy(sBody + n, tmp, (size_t)t);
        n += t;
    }
    sBody[n] = '\0';
    return n;
}

/* The client-generated greeting lives in the history as an AI turn; it must
   not be sent as an assistant message. */
static Boolean IsWelcomeMessage(const char* text)
{
    return text && strncmp(text, "AI initialized!", 15) == 0;
}

static Boolean BuildBody(void)
{
    const ProviderInfo* pi = ProviderInfoFor(sProvider);
    int n = 0;
    int cap = (int)sizeof(sBody);
    int count, i, first = 1;
    Boolean gemini = pi->usesGeminiDialect;

    sBody[0] = '\0';

    if (gemini) {
        /* Gemini has no model field, no stream flag and no max_tokens in the
           body: the model is part of the URL and SSE is requested with
           alt=sse.  Assistant turns are "model". */
        n = AppendRaw(n, "{\"contents\":[");
    } else {
        n = AppendRaw(n, "{\"model\":\"");
        n = AppendEscapedJSON(n, sModel, cap - 96);
        n = AppendRaw(n, "\"");
        if (!pi->usesOpenAIDialect)
            n = AppendRaw(n, ",\"max_tokens\":1024");
        n = AppendRaw(n, ",\"stream\":true,\"messages\":[");
    }

    count = gConversationHistory.count;
    if (count > kMaxConversationHistory)
        count = kMaxConversationHistory;

    for (i = 0; i < count; i++) {
        short idx = (short)((gConversationHistory.head + i) % kMaxConversationHistory);
        const ConversationMessage* m = &gConversationHistory.messages[idx];

        if (IsWelcomeMessage(m->text))
            continue;
        /* Leave room for the closing "\"}]}" and the next entry's prefix. */
        if (n > cap - 64)
            break;

        if (gemini) {
            n = AppendRaw(n, first ? "{\"role\":\"" : ",{\"role\":\"");
            n = AppendRaw(n, m->type == kUserMessage ? "user" : "model");
            n = AppendRaw(n, "\",\"parts\":[{\"text\":\"");
            n = AppendEscapedJSON(n, m->text, cap - 32);
            n = AppendRaw(n, "\"}]}");
        } else {
            n = AppendRaw(n, first ? "{\"role\":\"" : ",{\"role\":\"");
            n = AppendRaw(n, m->type == kUserMessage ? "user" : "assistant");
            n = AppendRaw(n, "\",\"content\":\"");
            n = AppendEscapedJSON(n, m->text, cap - 16);
            n = AppendRaw(n, "\"}");
        }
        first = 0;
    }

    n = AppendRaw(n, "]}");
    /* The per-message and per-escape limits above always leave room for the
       closing "]}", so what was built is complete JSON. */
    return true;
}

/* Parsed https base URL: host (no port), optional port, and a path with no
   leading scheme and no trailing slash. */
typedef struct {
    char           host[128];
    unsigned short port;
    char           path[128];
} ParsedBase;

static Boolean IsHostChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_';
}

/* Accepts https://host[:port][/path].  Everything is validated here, once,
   before the string is assembled or handed to the engine. */
static Boolean ParseBaseURL(const char* base, ParsedBase* out)
{
    const char* p;
    int hn = 0;
    unsigned long port = 0;

    out->host[0] = '\0';
    out->path[0] = '\0';
    out->port = 443;

    if (!base || !StartsWithCI(base, "https://")) {
        SetError("only https:// endpoints are supported");
        return false;
    }
    p = base + 8;

    while (*p && *p != ':' && *p != '/') {
        if (!IsHostChar(*p)) {
            SetError("invalid host in base URL");
            return false;
        }
        if (hn >= (int)sizeof(out->host) - 1) {
            SetError("host name too long");
            return false;
        }
        out->host[hn++] = *p++;
    }
    if (hn == 0) {
        SetError("base URL has no host");
        return false;
    }
    if (out->host[0] == '.' || strstr(out->host, "..") != NULL) {
        SetError("invalid host in base URL");
        return false;
    }
    out->host[hn] = '\0';

    if (*p == ':') {
        p++;
        if (*p < '0' || *p > '9') {
            SetError("invalid port in base URL");
            return false;
        }
        while (*p >= '0' && *p <= '9') {
            port = port * 10 + (unsigned long)(*p - '0');
            if (port > 65535) {
                SetError("port out of range in base URL");
                return false;
            }
            p++;
        }
        if (port == 0) {
            SetError("invalid port in base URL");
            return false;
        }
        out->port = (unsigned short)port;
    }

    if (*p == '/') {
        int pn = 0;
        while (*p && *p != '?' && *p != '#') {
            if (*p < 0x21 || *p > 0x7e) {
                SetError("invalid character in base URL path");
                return false;
            }
            if (pn >= (int)sizeof(out->path) - 1) {
                SetError("base URL path too long");
                return false;
            }
            out->path[pn++] = *p++;
        }
        if (*p == '?' || *p == '#') {
            SetError("base URL must not have a query or fragment");
            return false;
        }
        out->path[pn] = '\0';
        while (pn > 0 && out->path[pn - 1] == '/')
            out->path[--pn] = '\0';
    } else if (*p != '\0') {
        SetError("invalid base URL");
        return false;
    }

    return true;
}

static Boolean EndsWith(const char* s, const char* suffix)
{
    size_t n = strlen(s), m = strlen(suffix);

    return n >= m && strcmp(s + n - m, suffix) == 0;
}

static int AppendURL(int n, const char* s)
{
    int cap = (int)sizeof(sURL);

    while (*s && n < cap - 1)
        sURL[n++] = *s++;
    sURL[n] = '\0';
    return n;
}

/* Build the full request URL and the host passed to HttpClient::WarmUp.
   The base path is honoured: a path ending in /v1 (or /v1beta) does not get
   a second copy of that segment. */
static Boolean BuildURL(void)
{
    static ParsedBase u;
    const ProviderInfo* pi = ProviderInfoFor(sProvider);
    const char* configured = sBaseURL[0] ? sBaseURL : pi->defaultBase;
    char base[128];
    char tail[160];
    char portText[8];
    int need, n;

    CopyTrimmed(base, (short)sizeof(base), configured);
    if (!base[0]) {
        snprintf(sError, sizeof sError,
                 "%s provider needs a base URL - use File > Settings...", pi->name);
        return false;
    }
    if (!ParseBaseURL(base, &u)) {
        PrefixError(pi->name);
        return false;
    }

    if (pi->usesGeminiDialect) {
        const char* c;

        if (!sModel[0]) {
            snprintf(sError, sizeof sError,
                     "%s provider needs a model - use File > Settings...", pi->name);
            return false;
        }
        /* The model is a path segment here, so keep it to characters that
           are legal unencoded. */
        for (c = sModel; *c; c++) {
            if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
                  (*c >= '0' && *c <= '9') || *c == '-' || *c == '.' || *c == '_')) {
                snprintf(sError, sizeof sError,
                         "%s: model name has invalid characters", pi->name);
                return false;
            }
        }
        snprintf(tail, sizeof tail, "%s%s:streamGenerateContent?alt=sse",
                 EndsWith(u.path, "/v1beta") ? "models/" : "v1beta/models/", sModel);
    } else if (pi->usesOpenAIDialect) {
        snprintf(tail, sizeof tail, "%s",
                 EndsWith(u.path, "/v1") ? "chat/completions" : "v1/chat/completions");
    } else {
        snprintf(tail, sizeof tail, "%s",
                 EndsWith(u.path, "/v1") ? "messages" : "v1/messages");
    }

    need = 8 + (int)strlen(u.host) + (u.port != 443 ? 6 : 0) +
           (int)strlen(u.path) + 1 + (int)strlen(tail);
    if (need >= (int)sizeof(sURL)) {
        SetError("base URL too long");
        PrefixError(pi->name);
        return false;
    }

    n = AppendURL(0, "https://");
    n = AppendURL(n, u.host);
    if (u.port != 443) {
        snprintf(portText, sizeof portText, ":%u", (unsigned)u.port);
        n = AppendURL(n, portText);
    }
    n = AppendURL(n, u.path);
    n = AppendURL(n, "/");
    n = AppendURL(n, tail);

    /* WarmUp takes one host string and parses it as "https://<host>", so the
       port has to be in it: Uri::Port then drives the warm-up dial and the
       POST reuses that same connection. */
    CopyBounded(sHost, (short)sizeof(sHost), u.host);
    if (u.port != 443) {
        int hl = (int)strlen(sHost);
        snprintf(sHost + hl, sizeof(sHost) - (size_t)hl, ":%u", (unsigned)u.port);
    }
    return true;
}

/* ── Engine callbacks ────────────────────────────────────────────── */
/* The decoder ids are api_parse.h's kAPIDialect* values; openrouter and
   compatible share the OpenAI wire format. */
static int ParseDialect(void)
{
    if (sProvider == kProviderAnthropic)
        return kAPIDialectAnthropic;
    if (sProvider == kProviderGemini)
        return kAPIDialectGemini;
    return kAPIDialectOpenAI;
}

static void OnChunk(const char* at, int len)
{
    APIParse_Feed(&sParse, at, (short)len, ParseDialect());
}

static void OnComplete(HttpResponse& r)
{
    sCompleted = 1;
    if (!r.Success) {
        sFailed = 1;
        SetEngineError(r.ErrorMsg.c_str(), "request failed");
        return;
    }
    if (r.StatusCode >= 200 && r.StatusCode < 300)
        return;
    sFailed = 1;
    SetHttpError(r.StatusCode);
}

/* ── Public API ──────────────────────────────────────────────────── */
void API_Init(void)
{
    const char* v;
    APIProvider p = kProviderAnthropic;

    Prefs_Load();

    if (!API_ProviderFromName(Prefs_Get("provider", "anthropic"), &p))
        p = kProviderAnthropic;
    sProvider = p;

    /* An absent or empty model pref means "this provider's default"; a model
       the user typed is never replaced. */
    v = Prefs_Get("model", "");
    CopyBounded(sModel, (short)sizeof(sModel),
                (v && *v) ? v : ProviderInfoFor(sProvider)->defaultModel);
    CopyBounded(sKey, (short)sizeof(sKey), Prefs_Get("api_key", ""));
    CopyBounded(sBaseURL, (short)sizeof(sBaseURL), Prefs_Get("base_url", ""));

    v = Prefs_Get("tls_verify", "1");
    net68k_set_verify_peer((EqualsCI(v, "0") || EqualsCI(v, "false") ||
                            EqualsCI(v, "no") || EqualsCI(v, "off")) ? 0 : 1);

    sState = kAPIStateIdle;
    sError[0] = '\0';
}

void API_Shutdown(void)
{
    if (sState == kAPIStateConnecting || sState == kAPIStateStreaming)
        sHttp.CancelStream();
    sHttp.SslFreeAll();          /* close TLS, free contexts; ticket is kept */
    sState = kAPIStateIdle;
}

Boolean API_BeginRequest(void)
{
    const ProviderInfo* pi = ProviderInfoFor(sProvider);

    if (sState == kAPIStateConnecting || sState == kAPIStateStreaming) {
        SetError("a request is already in progress");
        return false;
    }
    sError[0] = '\0';

    if (!sKey[0]) {
        snprintf(sError, sizeof sError, "%s: no API key - use File > Settings...",
                 pi->name);
        return false;
    }
    if (!sModel[0]) {
        snprintf(sError, sizeof sError,
                 "%s provider needs a model - use File > Settings...", pi->name);
        return false;
    }
    if (!BuildURL())
        return false;
    if (!BuildBody()) {
        SetError("prompt too long");
        return false;
    }

    APIParse_Reset(&sParse, sText, (short)sizeof(sText));
    sTextSent = 0;
    sFinished = 0;
    sCompleted = 0;
    sFailed = 0;
    sCancelled = 0;

    if (sProvider == kProviderAnthropic) {
        sHttp.SetAuthorization(sKey);      /* engine emits "x-api-key: <key>" */
        CopyBounded(sHeaders, (short)sizeof(sHeaders),
                    "anthropic-version: 2023-06-01\r\n");
    } else if (sProvider == kProviderGemini) {
        sHttp.SetAuthorization("");
        snprintf(sHeaders, sizeof sHeaders, "x-goog-api-key: %s\r\n", sKey);
    } else if (sProvider == kProviderOpenRouter) {
        sHttp.SetAuthorization("");
        snprintf(sHeaders, sizeof sHeaders,
                 "Authorization: Bearer %s\r\n"
                 "HTTP-Referer: https://github.com/yocontra/macintosh-ai\r\n"
                 "X-Title: Macintosh AI\r\n", sKey);
    } else {
        sHttp.SetAuthorization("");
        snprintf(sHeaders, sizeof sHeaders, "Authorization: Bearer %s\r\n", sKey);
    }
    sHttp.SetExtraHeaders(sHeaders);

    sState = kAPIStateConnecting;

    /* Open the connection now (TCP + TLS handshake, cooperative); the POST is
       issued from API_Poll so this call does one job only. */
    if (!sHttp.WarmUp(sHost)) {
        snprintf(sError, sizeof sError, "%s: connection failed (network or TLS)",
                 pi->name);
        sState = kAPIStateError;
        return false;
    }
    return true;
}

short API_Poll(char* out, short outMax)
{
    short n = 0;

    if (!out || outMax <= 0)
        return 0;
    out[0] = '\0';

    if (sState == kAPIStateConnecting) {
        sState = kAPIStateStreaming;
        sHttp.PostStream(sURL, sBody, OnChunk, OnComplete);
        sFinished = 1;
        if (!sCompleted && !sCancelled) {
            sFailed = 1;
            snprintf(sError, sizeof sError,
                     "%s: connection failed (network or TLS)", CurrentProviderName());
        }
    }

    if (sState != kAPIStateStreaming)
        return 0;

    /* Hand over text the caller has not seen yet. */
    if (sTextSent < sParse.len) {
        short avail = (short)(sParse.len - sTextSent);
        short room = (short)(outMax - 1);
        if (avail > room)
            avail = room;
        memcpy(out, sText + sTextSent, (size_t)avail);
        sTextSent = (short)(sTextSent + avail);
        n = avail;
        out[n] = '\0';
    }

    if (sFinished && sTextSent >= sParse.len) {
        if (sCancelled)
            sState = kAPIStateIdle;
        else if (sFailed)
            sState = kAPIStateError;
        else
            sState = kAPIStateDone;
    }
    return n;
}

APIState API_State(void)
{
    return sState;
}

void API_Cancel(void)
{
    if (sState != kAPIStateConnecting && sState != kAPIStateStreaming)
        return;
    sHttp.CancelStream();
    sCancelled = 1;
    sTextSent = sParse.len;      /* drop anything the UI has not drawn yet */
    sState = kAPIStateIdle;
}

const char* API_LastError(void)
{
    return sError[0] ? sError : "unknown error";
}

APIProvider API_GetProvider(void) { return sProvider; }

void API_SetProvider(APIProvider p)
{
    const ProviderInfo* pi;
    unsigned int idx = (unsigned int)p;

    if (idx >= (unsigned int)kProviderCount)
        return;
    if (p == sProvider)
        return;

    pi = &kProviders[idx];
    sProvider = p;
    Prefs_Set("provider", pi->name);

    /* Follow the new provider's default model when the current model is one
       of the built-in defaults (or empty).  A model the user typed is kept. */
    if (!sModel[0] || IsBuiltInDefaultModel(sModel)) {
        CopyBounded(sModel, (short)sizeof(sModel), pi->defaultModel);
        Prefs_Set("model", sModel);
    }
    Prefs_Save();
}

const char* API_GetModel(void) { return sModel; }

void API_SetModel(const char* m)
{
    char tmp[64];
    CopyBounded(tmp, (short)sizeof(tmp), m);
    if (strcmp(tmp, sModel) == 0)
        return;
    strcpy(sModel, tmp);
    Prefs_Set("model", sModel);
    Prefs_Save();
}

const char* API_GetAPIKey(void) { return sKey; }

void API_SetAPIKey(const char* k)
{
    char tmp[128];
    CopyBounded(tmp, (short)sizeof(tmp), k);
    if (strcmp(tmp, sKey) == 0)
        return;
    strcpy(sKey, tmp);
    Prefs_Set("api_key", sKey);
    Prefs_Save();
}

const char* API_GetBaseURL(void) { return sBaseURL; }

void API_SetBaseURL(const char* u)
{
    char tmp[128];
    CopyBounded(tmp, (short)sizeof(tmp), u);
    if (strcmp(tmp, sBaseURL) == 0)
        return;
    strcpy(sBaseURL, tmp);
    Prefs_Set("base_url", sBaseURL);
    Prefs_Save();
}

Boolean API_GetCertificateCheck(void)
{
    return net68k_get_verify_peer() ? true : false;
}

void API_SetCertificateCheck(Boolean on)
{
    Boolean next = on ? true : false;

    net68k_set_verify_peer(next ? 1 : 0);
    /* The authmode is read when the mbedTLS config is built, so drop the
       cached TLS context (never mid-request) to make the change take effect
       on the next connect.  The session ticket is preserved by SslFreeAll. */
    if (sState != kAPIStateConnecting && sState != kAPIStateStreaming)
        sHttp.SslFreeAll();
    Prefs_Set("tls_verify", next ? "1" : "0");
    Prefs_Save();
}

/* One line for the chat window header: what is actually answering.  The
   middle dot is MacRoman 0xE1, which QuickDraw renders as U+00B7.  The
   header strip is narrow, so the line is capped at 45 bytes even when the
   caller's buffer is larger. */
static void SetStatusLine(char* out, short max, const char* text)
{
    short cap = max;

    if (cap > 46)
        cap = 46;              /* 45 visible bytes + NUL */
    CopyBounded(out, cap, text);
}

void API_GetStatusText(char* out, short max)
{
    char tmp[96];

    if (!out || max <= 0)
        return;

    if (gActiveAIModel == kMarkovModel) {
        SetStatusLine(out, max, "Markov chain");
        return;
    }
    if (gActiveAIModel == kTemplateModel) {
        SetStatusLine(out, max, "Template model");
        return;
    }

    if (!sKey[0]) {
        SetStatusLine(out, max, "Remote AI \xE1 no API key");
        return;
    }
    if (!net68k_get_verify_peer()) {
        SetStatusLine(out, max, "Remote AI \xE1 TLS: no cert check");
        return;
    }
    if (!sModel[0]) {
        SetStatusLine(out, max, "Remote AI \xE1 no model");
        return;
    }
    snprintf(tmp, sizeof tmp, "Remote AI \xE1 %s", sModel);
    SetStatusLine(out, max, tmp);
}
