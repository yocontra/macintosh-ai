/* ═══════════════════════════════════════════════════════════════════
   HttpClient.h — BOILERPLATE. Do not modify when porting to a new API.
   TLS/HTTPS client for classic Mac 68k (mbedTLS + MacTCP).
   To use: call PostStream() from your ApiSend() implementation.
   ═══════════════════════════════════════════════════════════════════ */
#ifndef __HTTP_CLIENT__
#define __HTTP_CLIENT__

#include "HttpResponse.h"
#include "Uri.h"
#include <functional>

using namespace std;

// Disabled strict HTTP parsing to improve performance
#ifndef HTTP_PARSER_STRICT
#define HTTP_PARSER_STRICT 0
#endif

#ifndef http_parser_h
extern "C"
{
	#include "http_parser.h"
}
#endif

typedef enum {
    kConnState_Uninitialized = 0, /* first time, nothing allocated      */
    kConnState_Idle,              /* TCP+TLS up, no request in flight   */
    kConnState_Connecting,        /* TCP up, TLS handshake pending      */
    kConnState_Dead               /* must reconnect before next use     */
} ConnState;

#ifdef SSL_ENABLED
extern "C"
{

	//#include <mbedtls/net_sockets.h>
	#include <mbedtls/debug.h>
	#include <mbedtls/ssl.h>
	#include <mbedtls/entropy.h>
	#include <mbedtls/ctr_drbg.h>
	#include <mbedtls/error.h>
	#include <mbedtls/ssl_ciphersuites.h>
	#include <mbedtls/net_sockets.h>
}
#endif

class HttpClient
{
public:
	enum RequestStatus
	{
		Idle,
		Waiting,
		Running
	};
	
	HttpClient();
	HttpClient(string baseUri);
	void Get(const string& requestUri, std::function<void(HttpResponse&)> onComplete);
	void Post(const string& requestUri, const string& content, std::function<void(HttpResponse&)> onComplete);
	void PostStream(const string& requestUri, const string& content,
	                std::function<void(const char*, int)> onChunk,
	                std::function<void(HttpResponse&)> onComplete);
	void Patch(const string& requestUri, const string& content, std::function<void(HttpResponse&)> onComplete);
	void PatchWithBody(const string& requestUri, const string& content, std::function<void(HttpResponse&)> onComplete);
	void Get(const Uri& requestUri, std::function<void(HttpResponse&)> onComplete);
	void Post(const Uri& requestUri, const string& content, std::function<void(HttpResponse&)> onComplete);
	void SetAuthorization(string authorization);
	void SetExtraHeaders(const char* headers);  /* appended after Authorization */
	void SetYieldHook(void (*hook)());          /* called inside MacYield; removes compile-time coupling to UI */
	void ProcessRequests();
	void InitThread();
	bool WarmUp(const string& host);
	/* True when the last (failed) handshake was the server rejecting a stale
	   session ticket — a normal, recoverable condition (the ticket is already
	   cleared). Callers should retry once with a full handshake. Cleared on
	   each WarmUp entry. */
	bool ResumeRejected() const { return _resumeRejected; }
	/* Abort the in-flight request (e.g. user pressed Esc / Cmd-. during
	   streaming). Safe to call from a yield hook: only sets cancel flags;
	   the transfer unwinds, the connection is marked Dead, and the
	   onComplete callback is NOT invoked. No-op when nothing is in flight. */
	void CancelStream();
	/* Seconds since the saved TLS session ticket was issued, or -1 when no
	   ticket is saved. Used for proactive idle refresh before the server's
	   ~24h ticket lifetime expires.                                        */
	long TicketAgeSeconds();
	void SslFreeAll();
	void ForceReconnect(); /* mark connection Dead so next request uses a fresh TCP stream */
	/* Release the response body buffer after the caller has finished parsing it.
	   string::clear() keeps capacity; swap with empty actually frees the heap block. */
	void ReleaseContent() { string().swap(_response.Content); }

private:
	static const int BUF_SIZE = 4096;
	string _baseUri;
	string _proxyHost;
	string _stunnelHost;
	string _authorization;
	string _extraHeaders;
	Uri _uri;
	string _request;
	RequestStatus _status;
	HttpResponse _response;
	std::function<void(HttpResponse&)> _onComplete;
	int _proxyPort;
	int _stunnelPort;
	int _debugLevel;
	bool _cancel;
	int  _redirectDepth;
	bool _resumeRejected = false;  /* see ResumeRejected() */

	void Init(string baseUri);
	void AppendAuthHeader(string& req); // §8 DEDUP: single update point for Notion-Version header
	bool TryGetUri(const string& requestUri, Uri& uri, std::function<void(HttpResponse&)> onComplete); // §9 DEDUP: shared try/catch for string wrappers
	Uri GetUri(const string& requestUri);
	string GetRemoteHost(const Uri& uri);
	int GetRemotePort(const Uri& uri);
	void Connect(const Uri& uri, unsigned long stream);
	void Put(const Uri& requestUri, const string& content, function<void(HttpResponse&)> onComplete);
	void PutPost(const Uri& requestUri, const string& httpMethod, const string& content, function<void(HttpResponse&)> onComplete);
	void Request(const Uri& uri, string request, function<void(HttpResponse&)> onComplete);
	bool DoRedirect();
	void InitParser();
	static void Yield();
	void HttpRequest();

	http_parser _parser;
	http_parser_settings _settings;

	unsigned long _stream;
	
	bool Connect();
	bool Request();
	bool Response();
	void NetClose();

	const char* _cRequest;

	#ifdef SSL_ENABLED
	mbedtls_net_context _server_fd;
	mbedtls_ssl_context _ssl;
	mbedtls_ssl_config _conf;
	mbedtls_ssl_session _savedSession;
	mbedtls_x509_crt _cacert;
	mbedtls_entropy_context _entropy;
	mbedtls_ctr_drbg_context _ctr_drbg;
	bool _hasSavedSession;
	bool _lastHandshakeOk;        /* set by SslHandshake; guards session save in SslClose */
	bool _ticketDirty;            /* true after new handshake; cleared after SaveSessionToDisk */
	ConnState      _connState;
	unsigned long  _cachedIpAddress;      /* cached from DNS (OPT-2)          */
	long           _sessionEstablishedTicks; /* TickCount when session saved   */
	unsigned long  _ticketSavedDateTime = 0; /* GetDateTime secs when ticket saved (survives via prefs) */
	bool           _sslInited;            /* SslInitContexts() done once       */
	bool           _dnsNeedsSave;         /* DNS cache needs flush to disk     */
	long           _lastUseTicks = 0;     /* TickCount of last handshake/request on this conn */
	bool           _connServedRequest = false; /* conn served ≥1 request → server honours keep-alive */

	void HttpsRequest();
	bool SslInitContexts();
	void SaveNextSeed();      /* random seed for the next launch      */
	void FreshenRandom();     /* fold in fresh notes before a key      */
	bool SslConfigSetup();
	bool ResolveAndCreateStream(); // §10 DEDUP: shared DNS+CreateStream logic for TcpConnect/TcpReconnect
	bool TcpConnect();
	bool TcpReconnect();
	bool TcpConnectionAlive();
	bool IsSessionExpired();
	void ArmResumeIfFresh();    /* offer saved ticket unless locally stale (post-SslConfigSetup only) */
	bool SslConnect();
	bool SslHandshake();
	bool SslVerifyCert();
	bool SslRequest();
	bool SslResponse();
	void SslClose();
	void SaveSessionToDisk();   /* persist ticket so restarts skip cold handshake */
	void LoadSessionFromDisk(); /* restore ticket saved by previous run           */

	int _overrideCipherSuite[2] = { 0, 0 };
	int _cipherSuites[13] = // 12 suites + 1 zero terminator; static_assert in HttpClient.cpp guards count
	{
		/* OPT #9: ChaCha20-Poly1305 only AEAD offered — software AES-GCM is
		 * the slow path on 68k (AES built with ROM_TABLES+FEWER_TABLES,
		 * GHASH is table-based GF(2^128)).  Cloudflare ignores client suite
		 * order (verified Jun 2026: chacha-first offer still got AES-GCM),
		 * so GCM suites are removed entirely; CF then picks ChaCha over the
		 * CBC fallbacks (verified via openssl s_client probe). */
		MBEDTLS_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
		MBEDTLS_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
		MBEDTLS_TLS_RSA_WITH_AES_128_CBC_SHA,
		MBEDTLS_TLS_RSA_WITH_AES_128_CBC_SHA256,
		MBEDTLS_TLS_RSA_WITH_AES_256_CBC_SHA,
		MBEDTLS_TLS_RSA_WITH_AES_256_CBC_SHA256,
		MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA,
		MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA,
		MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256,
		MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256,
		MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA,
		MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA,
		0
	};
	#endif // SSL_ENABLED
};

#endif