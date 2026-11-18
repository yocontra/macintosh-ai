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

static char sModel[64]   = "";
static char sKey[128]    = "";
static char sBaseURL[128] = "";
static char sError[128];

static char sURL[192];
static char sHost[64];
static char sHeaders[192];
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
        SetError("TLS: certificate not trusted");
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
        SetError("API key rejected - check Settings");
    else if (status == 429)
        SetError("rate limited by the provider");
    else if (status >= 500)
        SetError("provider server error");
    else
        snprintf(sError, sizeof sError, "HTTP %u", status);
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
    int n = 0;
    int cap = (int)sizeof(sBody);
    int count, i, first = 1;

    sBody[0] = '\0';
    n = AppendRaw(n, "{\"model\":\"");
    n = AppendEscapedJSON(n, sModel, cap - 96);
    n = AppendRaw(n, "\"");
    if (sProvider == kProviderAnthropic)
        n = AppendRaw(n, ",\"max_tokens\":1024");
    n = AppendRaw(n, ",\"stream\":true,\"messages\":[");

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

        n = AppendRaw(n, first ? "{\"role\":\"" : ",{\"role\":\"");
        n = AppendRaw(n, m->type == kUserMessage ? "user" : "assistant");
        n = AppendRaw(n, "\",\"content\":\"");
        n = AppendEscapedJSON(n, m->text, cap - 16);
        n = AppendRaw(n, "\"}");
        first = 0;
    }

    n = AppendRaw(n, "]}");
    /* The per-message and per-escape limits above always leave room for the
       closing "]}", so what was built is complete JSON. */
    return true;
}

static Boolean BuildURL(void)
{
    static const char* kAnthropicBase = "https://api.anthropic.com";
    static const char* kOpenAIBase = "https://api.openai.com";
    const char* base = sBaseURL[0] ? sBaseURL : (sProvider == kProviderOpenAI ? kOpenAIBase : kAnthropicBase);
    const char* path = (sProvider == kProviderOpenAI) ? "/v1/chat/completions" : "/v1/messages";
    const char* h;
    int n, hn = 0;

    if (!StartsWithCI(base, "https://")) {
        SetError("only https:// endpoints are supported");
        return false;
    }
    base += 8;   /* skip the scheme; it is written out lowercase below */

    n = (int)strlen(base);
    if (n >= (int)sizeof(sURL) - 32) {
        SetError("base URL too long");
        return false;
    }

    /* The engine only ever connects to 443 for https (Uri carries no port),
       so an explicit other port would silently go to the wrong place. */
    h = base;
    while (*h && *h != '/' && *h != ':')
        h++;
    if (*h == ':') {
        const char* p = h + 1;
        if (!(p[0] == '4' && p[1] == '4' && p[2] == '3' && (p[3] == '/' || p[3] == '\0'))) {
            SetError("only port 443 is supported");
            return false;
        }
    }

    strcpy(sURL, "https://");
    strcat(sURL, base);
    n = (int)strlen(sURL);
    while (n > 8 && sURL[n - 1] == '/')
        sURL[--n] = '\0';
    strcat(sURL, path);

    h = sURL + 8;
    while (*h && *h != '/' && *h != ':') {
        if (hn >= (int)sizeof(sHost) - 1) {
            SetError("host name too long");
            return false;
        }
        sHost[hn++] = *h++;
    }
    sHost[hn] = '\0';
    if (hn == 0) {
        SetError("base URL has no host");
        return false;
    }
    return true;
}

/* ── Engine callbacks ────────────────────────────────────────────── */
static void OnChunk(const char* at, int len)
{
    APIParse_Feed(&sParse, at, (short)len, (int)sProvider);
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
    Prefs_Load();

    sProvider = StartsWithCI(Prefs_Get("provider", "anthropic"), "openai")
                    ? kProviderOpenAI : kProviderAnthropic;
    CopyBounded(sModel, (short)sizeof(sModel), Prefs_Get("model", "claude-sonnet-4-5"));
    CopyBounded(sKey, (short)sizeof(sKey), Prefs_Get("api_key", ""));
    CopyBounded(sBaseURL, (short)sizeof(sBaseURL), Prefs_Get("base_url", ""));

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
    if (sState == kAPIStateConnecting || sState == kAPIStateStreaming) {
        SetError("a request is already in progress");
        return false;
    }
    sError[0] = '\0';

    if (!sKey[0]) {
        SetError("no API key - use File > Settings...");
        return false;
    }
    if (!BuildURL())
        return false;
    if (!sModel[0]) {
        SetError("no model configured - use File > Settings...");
        return false;
    }
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

    if (sProvider == kProviderOpenAI) {
        sHttp.SetAuthorization("");
        snprintf(sHeaders, sizeof sHeaders, "Authorization: Bearer %s\r\n", sKey);
    } else {
        sHttp.SetAuthorization(sKey);      /* engine emits "x-api-key: <key>" */
        strcpy(sHeaders, "anthropic-version: 2023-06-01\r\n");
    }
    sHttp.SetExtraHeaders(sHeaders);

    sState = kAPIStateConnecting;

    /* Open the connection now (TCP + TLS handshake, cooperative); the POST is
       issued from API_Poll so this call does one job only. */
    if (!sHttp.WarmUp(sHost)) {
        SetError("connection failed (network or TLS)");
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
            SetError("connection failed (network or TLS)");
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
    APIProvider next = (p == kProviderOpenAI) ? kProviderOpenAI : kProviderAnthropic;
    if (next == sProvider)
        return;
    sProvider = next;
    Prefs_Set("provider", sProvider == kProviderOpenAI ? "openai" : "anthropic");
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
