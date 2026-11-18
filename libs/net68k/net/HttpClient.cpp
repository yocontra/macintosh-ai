#include <ctype.h>
#include <string.h>
#include "HttpClient.h"
#include <stdio.h>
#include <Memory.h>


#ifdef SSL_ENABLED
#include "RootCert.h"
#include <mbedtls/platform_util.h>
#endif
#include "mac_entropy.h"

extern "C"
{
#include <mbedtls/net_sockets.h>
	#include <MacTCP.h>
	#include <mactcp/CvtAddr.h>
	#include <mactcp/TCPHi.h>
	extern OSErr gLastMacTCPErr;
	extern OSErr gLastRecvErr;
	extern unsigned short gLastAmtUnread;
	int uecc_precompute_x25519_key(int (*f_rng)(void *, unsigned char *, size_t), void *p_rng);
	int uecc_x25519_pre_ready(void);
	extern int g_last_ecdh_grp_id;
	extern size_t g_pool_peak;
	extern unsigned int g_pool_overflow_count;
	/* OPT-3: persistent ECDSA cache lifecycle */
	void uecc_load_persist_ecdsa_cache(void);
	void uecc_save_persist_ecdsa_cache(void);
	int  uecc_persist_ecdsa_dirty(void);
	/* Shamir2 table: precompute 2-bit windowed Shamir table for fast SKE verify */
	void uecc_precompute_shamir2_table(void);
	/* OPT-6: async TCP helpers */
	int   tcp_open_async(unsigned long stream, unsigned long ipAddress, short port);
	OSErr tcp_wait_connected(GiveTimePtr giveTime, bool *cancel);
	short tcp_connection_state(unsigned long stream);
	/* Zeroed-pBlock TCPCreate — avoids connectionExists on real hardware */
	OSErr tcp_create_stream(unsigned long *stream, unsigned long recvLen,
	                        GiveTimePtr giveTime, int *cancel);
	/* Cancel flag in net_sockets_mac.c — aborts in-flight transfers */
	void net_mac_set_cancel(int c);
	/* OPT-6: yield hooks */
	void uECC_set_yield(void (*f)(void));
	/* Canary check for kDemoPageID corruption diagnosis */

	void fast_alloc_set_yield(void (*f)(void));
}

#include "tls_cache.h"
#include <Threads.h>
#include <Events.h>

/* Certificate verification is on by default. The application clears it only
 * when the user asks to talk to a server this build has no trust anchor for. */
static bool sVerifyPeer = true;

extern "C" void net68k_set_verify_peer(int on) { sVerifyPeer = (on != 0); }
extern "C" int  net68k_get_verify_peer(void)    { return sVerifyPeer ? 1 : 0; }
static void (*sYieldHook)() = nullptr;
/* Every yield also notes the time: when the network lets us go is
   something only this Mac sees (mac_entropy.c). */
static void MacYield(void) { mac_entropy_note(0); SystemTask(); YieldToAnyThread(); if (sYieldHook) sYieldHook(); }

extern void DebugLog(const char* func, const char* msg);

/* MacTCPHelper's ConvertStringToAddr strcpy()s the canonical name
   (hInfo.cname) back over the hostname it was given. For a host behind a
   CNAME that name is longer (api.spotify.com -> edge-web.dual-gslb.
   spotify.com.), so passing host.c_str() wrote past that buffer and
   corrupted the heap (found in MacPlayer, Oct 2026). Resolve through a
   scratch buffer that holds any cname (hostInfo's is 255 bytes), with a
   spare byte in front for its name[strlen(name)-1] on an empty cname.
   api.anthropic.com / api.notion.com have no CNAME, which is why this
   never showed here. */
static OSErr ResolveHost(const string& host, unsigned long* ip)
{
	char buf[1 + 256];
	char* name = buf + 1;
	strncpy(name, host.c_str(), 255);
	name[255] = '\0';
	return ConvertStringToAddr(name, ip, (GiveTimePtr)MacYield);
}

HttpClient::HttpClient()
{
	Init("");
}

HttpClient::HttpClient(string baseUri)
{
	Init(baseUri);
}

/* Public functions */
bool HttpClient::TryGetUri(const string& requestUri, Uri& uri, function<void(HttpResponse&)> onComplete)
{
	_redirectDepth = 0; // §redirect-fix: reset here — all public string-wrapper entry points go through TryGetUri
	uri = GetUri(requestUri);
	if (!uri.Valid) {
		HttpResponse r;
		r.ErrorMsg = "Invalid uri";
		onComplete(r);
		return false;
	}
	return true;
}

// §1 DEDUP: string overloads all share the same parse-then-dispatch pattern
void HttpClient::Get(const string& requestUri, function<void(HttpResponse&)> onComplete)
{
	Uri uri;
	if (TryGetUri(requestUri, uri, onComplete))
		Get(uri, onComplete);
}

void HttpClient::AppendAuthHeader(string& req) // §8 DEDUP: single update point for auth headers
{
	if (!_authorization.empty()) {
		req += "x-api-key: ";
		req += _authorization;
		req += "\r\n";
	}
	if (!_extraHeaders.empty()) {
		req += _extraHeaders;
	}
}

void HttpClient::SetExtraHeaders(const char* headers) { _extraHeaders = headers ? headers : ""; }
void HttpClient::SetYieldHook(void (*hook)()) { sYieldHook = hook; }

void HttpClient::CancelStream()
{
	if (_status == Idle) return;          /* nothing in flight */
	_cancel = true;                       /* skips _onComplete on unwind */
	net_mac_set_cancel(1);                /* aborts the MacTCP recv/send poll loops */
#ifdef SSL_ENABLED
	_connState = kConnState_Dead;         /* mid-body abort leaves unread data in stream */
#endif
}

void HttpClient::Get(const Uri& requestUri, function<void(HttpResponse&)> onComplete)
{
	DebugLog("Get", requestUri.Host.c_str());
	string req;
	req.reserve(128 + requestUri.Path.size() + requestUri.Host.size() + _authorization.size());
	req  = "GET ";
	req += requestUri.Path;
	req += " HTTP/1.1\r\nHost: ";
	req += requestUri.HostHeader();
	req += "\r\n";
	AppendAuthHeader(req); // §8 DEDUP
	req += "Connection: keep-alive\r\nKeep-Alive: timeout=90, max=100\r\n"
	       "User-Agent: MacHTTP/1.0\r\n\r\n";
	Request(requestUri, req, onComplete);
}

void HttpClient::Post(const string& requestUri, const string& content, function<void(HttpResponse&)> onComplete)
{
	Uri uri; // §1 DEDUP: same parse-then-dispatch pattern as Get/Patch/PatchWithBody
	if (TryGetUri(requestUri, uri, onComplete))
		Post(uri, content, onComplete);
}

void HttpClient::Patch(const string& requestUri, const string& content, function<void(HttpResponse&)> onComplete)
{
	Uri uri; // §1 DEDUP
	if (TryGetUri(requestUri, uri, onComplete))
		Put(uri, content, onComplete);
}

void HttpClient::PatchWithBody(const string& requestUri, const string& content, function<void(HttpResponse&)> onComplete)
{
	Uri uri; // §1 DEDUP
	if (TryGetUri(requestUri, uri, onComplete))
		PutPost(uri, "PATCH", content, onComplete);
}

void HttpClient::Post(const Uri& requestUri, const string& content, function<void(HttpResponse&)> onComplete)
{
	string method = "POST";
	PutPost(requestUri, method, content, onComplete);
}

void HttpClient::PostStream(const string& requestUri, const string& content,
	std::function<void(const char*, int)> onChunk,
	std::function<void(HttpResponse&)> onComplete)
{
	Uri uri;
	if (!TryGetUri(requestUri, uri, onComplete)) return;
	_response.onChunk = onChunk;
	PutPost(uri, "POST", content, onComplete);
}

void HttpClient::Put(const Uri& requestUri, const string& content, function<void(HttpResponse&)> onComplete)
{
	// §7 DEDUP: delegate to PutPost — discardBody set first so Request()'s save/restore preserves it
	_response.discardBody = true; /* OPT-8: don't accumulate PATCH response body */
	PutPost(requestUri, "PATCH", content, onComplete);
}

void HttpClient::PutPost(const Uri& requestUri, const string& method, const string& content, function<void(HttpResponse&)> onComplete)
{
	string lenStr = to_string(content.length());
	string req;
	req.reserve(160 + method.size() + requestUri.Path.size() + requestUri.Host.size()
	                + _authorization.size() + lenStr.size() + content.size());
	req  = method;
	req += ' ';
	req += requestUri.Path;
	req += " HTTP/1.1\r\nHost: ";
	req += requestUri.HostHeader();
	req += "\r\n";
	AppendAuthHeader(req); // §8 DEDUP
	req += "Content-Type: application/json\r\n"
	       "Connection: keep-alive\r\nKeep-Alive: timeout=90, max=100\r\nContent-Length: ";
	req += lenStr;
	req += "\r\n\r\n";
	req += content;
	Request(requestUri, std::move(req), onComplete);
}

void HttpClient::SetAuthorization(string authorization)
{
	_authorization = authorization;
}

/* Private functions */
void HttpClient::Init(string baseUri)
{
	// §16 ARCH: verify _cipherSuites count matches the declared size [13]
	static_assert(sizeof(_cipherSuites) / sizeof(int) == 13,
	              "_cipherSuites count mismatch: update [13] in HttpClient.h");
	/* Enlarge the stack to 64 KB before the heap expands.
	 * SetApplLimit MUST be called before MaxApplZone — once MaxApplZone
	 * claims free memory as heap, lowering ApplLimit corrupts the heap.
	 * This constructor runs from the global _httpClient before main(), so
	 * this is the only reliable place to call SetApplLimit in time.
	 * The static guard ensures we only do this once.                    */
	static bool sStackEnlarged = false;
	if (!sStackEnlarged) {
		sStackEnlarged = true;
		SetApplLimit(GetApplLimit() - (104L * 1024L));  /* 24 KB → 128 KB */
	}
	MaxApplZone();

	_baseUri = baseUri;
	_proxyHost = "";
	_stunnelHost = "";
	_authorization = "";
	_proxyPort = 5000;
	_stunnelPort = 0;
	_debugLevel = 0;
	_redirectDepth = 0;
	_status = Idle;
	InitParser();

	#ifdef SSL_ENABLED
	_overrideCipherSuite[0] = 0;
	mbedtls_ssl_session_init(&_savedSession);
	_hasSavedSession = false;
	_lastHandshakeOk = false;
	_ticketDirty     = false;
	_connState = kConnState_Uninitialized;
	_cachedIpAddress = 0;
	_sessionEstablishedTicks = 0;
	_sslInited = false;
	_dnsNeedsSave = false;
	#endif
}

void HttpClient::Yield()
{
	SystemTask();
}

void HttpClient::Connect(const Uri& uri, unsigned long stream)
{
	HttpResponse response;

	string request =
		"CONNECT " + uri.Host + " HTTP/1.1\r\n" +
		"Host: " + uri.Host + "\r\n" +
		"User-Agent: MacHTTP/1.0\r\n\r\n";

	SendData(
		stream,
		(Ptr)request.c_str(),
		(unsigned short)request.size(), // §13 PERF: .size() is O(1); strlen is O(n)
		false,
		(GiveTimePtr)MacYield,
		&_cancel);
}

Uri HttpClient::GetUri(const string& requestUri)
{
	if (!Uri::IsAbsolute(requestUri))
	{
		string absUri = _baseUri + requestUri;
		return Uri(absUri);
	}
	return Uri(requestUri);
}

void HttpClient::Request(const Uri& uri, string request, function<void(HttpResponse&)> onComplete)
{
	DebugLog("Request", uri.Path.c_str());
	_uri = uri;
	_request = std::move(request);
	_onComplete = onComplete;
	_cRequest = NULL;
	_cancel = false;
	net_mac_set_cancel(0); /* clear any leftover cancel from a previous abort */
	/* Note: _redirectDepth is NOT reset here — DoRedirect() increments it before
	   calling back into the request chain, so resetting here would defeat the limit.
	   Depth is reset to 0 in the public string-wrapper entry points (user-initiated
	   requests) and in Init() (construction). */
	/* keepDiscardBody=true so Put()'s pre-set discardBody survives Reset() */
	_response.Reset(/*keepDiscardBody=*/true);
	_status = Waiting;

	memset(&_parser, 0, sizeof(_parser));
	_parser.data = (void*)&_response;
	http_parser_init(&_parser, HTTP_RESPONSE);

	InitThread();

	/* Clear discardBody after the request completes so a PATCH's flag doesn't
	   leak into the next GET (which runs via a separate Request() call). */
	_response.discardBody = false;
}

void HttpClient::InitThread()
{
	DebugLog("InitThread", _uri.Scheme.c_str());
	_status = Running;

	if (_uri.Scheme == "http" ||
		(_stunnelHost != "" && _uri.Scheme == "https"))
	{
		HttpRequest();
	}
	#ifdef SSL_ENABLED
	else
	{
		HttpsRequest();
	}
	#endif
}

void HttpClient::ProcessRequests()
{
	SystemTask();
}

void HttpClient::HttpRequest()
{
	DebugLog("HttpRequest", "start");
	if (Connect())
		if (Request())
			Response();

	NetClose();
}

#ifdef SSL_ENABLED
bool HttpClient::WarmUp(const string& host)
{
	DebugLog("WarmUp", "entry");
	_uri    = Uri("https://" + host);
	_cancel = false;
	net_mac_set_cancel(0);
	_resumeRejected = false;
	_response.Reset();

	/* SslConnect initialises everything on first call (Uninitialized → Connecting),
	 * or finds the connection still Idle from a previous WarmUp (no-op). */
	if (!SslConnect()) {
		DebugLog("WarmUp", ("SslConnect fail: " + _response.ErrorMsg.substr(0,40)).c_str());
		_connState = kConnState_Dead;
		return false;
	}
	/* Full handshake needed for Connecting state (Idle = already warmed up). */
	if (_connState == kConnState_Connecting) {
		bool hsOk = SslHandshake();
		if (!hsOk) {
			_connState = kConnState_Dead;
			return false;
		}
		_connState = kConnState_Idle;
		_lastUseTicks = TickCount(); /* idle-age clock starts at handshake */

		/* Served ping: Cloudflare drops a connection that never served a
		 * request within ~6 s, but honours Keep-Alive: timeout=90 once it
		 * has served one (hardware-verified, Jun 2026).  One throwaway GET
		 * here — in background warm-up, conn at its freshest — flips the
		 * connection to "served", so the user's first message within 90 s
		 * reuses it directly (~0.2 s to ssl_write) instead of paying the
		 * ~4-6 s reconnect+resume.  Failure is harmless: mark Dead and the
		 * first message falls back to reconnect+resume as before.          */
		_request  = "GET / HTTP/1.1\r\nHost: " + GetRemoteHost(_uri) +
		            "\r\nConnection: keep-alive\r\n\r\n";
		_cRequest = NULL;
		_response.Reset();
		InitParser();
		memset(&_parser, 0, sizeof(_parser));
		_parser.data = (void*)&_response;
		http_parser_init(&_parser, HTTP_RESPONSE);
		if (SslRequest() && SslResponse() && _connState == kConnState_Idle) {
			_connServedRequest = true;
			_lastUseTicks = TickCount();
			DebugLog("WarmUp", "served ping ok");
		} else {
			DebugLog("WarmUp", "served ping fail");
			_connState = kConnState_Dead; /* next request reconnects+resumes */
		}
		_response.Reset();

		/* Save session ticket now (OPT-5): WarmUp skips SslClose (OPT-1) so
		 * without this save the ticket is never stored.  If the warm-up
		 * connection goes stale before pageRequest uses it, the Dead-path
		 * retry finds the ticket and resumes in ~1s instead of a full 13s. */
		mbedtls_ssl_session_free(&_savedSession);
		mbedtls_ssl_session_init(&_savedSession);
		if (mbedtls_ssl_get_session(&_ssl, &_savedSession) == 0) {
			_hasSavedSession = true;
			_sessionEstablishedTicks = TickCount();
			SaveSessionToDisk();
			_ticketDirty = false; /* saved; SslClose on subsequent requests can skip */
			DebugLog("WarmUp", "session saved to disk");
		}
	}

	/* Connection is now Idle — leave it alive for the first real request.
	 * OPT-1: no SslClose() here; TCP+TLS persists for pageRequest(). */

	/* Replenish the X25519 keypair the handshake just consumed (uecc_alt
	 * precompute is one-shot).  WarmUp skips SslClose, so without this the
	 * first idle-expired reconnect pays a full donna keygen (~3.4s Classic II,
	 * ~2s SE/30) synchronously in the request path — measured as the Jun 11
	 * "TcpReconnect 4s" gap.  Here it runs in background warm-up instead. */
	if (!uecc_x25519_pre_ready()) {
		uECC_set_yield(MacYield);
		fast_alloc_set_yield(MacYield);
		FreshenRandom();
		uecc_precompute_x25519_key(mbedtls_ctr_drbg_random, &_ctr_drbg);
		uECC_set_yield(0);
		fast_alloc_set_yield(0);
		DebugLog("WarmUp", "x25519 replenished");
	}

	DebugLog("WarmUp", "idle ok");
	return true;
}

void (*gHCTraceFn)(const char *msg) = 0;

void HttpClient::HttpsRequest()
{
	DebugLog("HttpsRequest", "start");
	bool retried = false;

HttpsRequest_connect:
	if (!SslConnect()) {
		/* SslConnect already set _response.ErrorMsg */
		if (!DoRedirect()) {
			_response.discardBody = false;
			if (!_cancel) _onComplete(_response);
			else _cancel = false;
		}
		return;
	}

	/* OPT-1: skip handshake when reusing an existing TLS session (Idle state) */
	bool wasIdle = (_connState == kConnState_Idle);
	if (_connState == kConnState_Connecting) {
		_resumeRejected = false;
		bool hsOk = SslHandshake() && SslVerifyCert();
		if (!hsOk) {
			/* Server refused the saved session ticket (e.g. after an idle
			 * spell or a key rotation): SslHandshake already dropped it, so
			 * one retry is a clean full handshake — the same recovery the
			 * launch-time WarmUp does, instead of showing the Connection
			 * Error dialog.  Never retry a canceled request. */
			if (_resumeRejected && !retried && !_cancel) {
				DebugLog("HttpsRequest", "resume rejected, full handshake retry");
				goto HttpsRequest_stale;
			}
			SslClose();
			return;
		}
		_connState = kConnState_Idle;
		_lastUseTicks = TickCount(); /* idle-age clock starts at handshake */
	}

	if (!SslRequest()) {
		/* SslRequest failed on a reused connection (half-open TCP).
		 * Retry once with a fresh handshake before surfacing the error.
		 * Never retry a canceled request.                                */
		if (wasIdle && !retried && !_cancel) goto HttpsRequest_stale;
		SslClose();
		return;
	}
	if (!SslResponse()) {
		if (wasIdle && !retried && !_cancel) goto HttpsRequest_stale;
		SslClose();
		return;
	}
	/* SslResponse returned true but server closed without sending a response:
	 * PEER_CLOSE_NOTIFY with no message parsed → stale keep-alive connection. */
	if (wasIdle && !retried && !_response.Success && _connState == kConnState_Dead)
		goto HttpsRequest_stale;
	SslClose();
	return;

HttpsRequest_stale:
	DebugLog("HttpsRequest", "stale conn retry");
	retried = true;
	_cRequest = NULL;
	{ bool savedDiscard = _response.discardBody;
	  _response.Reset();
	  _response.discardBody = savedDiscard; }
	memset(&_parser, 0, sizeof(_parser));
	_parser.data = (void*)&_response;
	http_parser_init(&_parser, HTTP_RESPONSE);
	_connState = kConnState_Dead;
	goto HttpsRequest_connect;
}
#endif // SSL_ENABLED

/* Forward declarations for static callbacks (defined at end of file) */
static int on_status_callback(http_parser* parser, const char *at, size_t length);
static int on_header_field_callback(http_parser* parser, const char *at, size_t length);
static int on_header_value_callback(http_parser* parser, const char *at, size_t length);
static int on_headers_complete_callback(http_parser* parser);
static int on_message_complete_callback(http_parser* parser);
static int on_body_callback(http_parser* parser, const char *at, size_t length);

void HttpClient::InitParser()
{
	_parser.data = (void*)&_response;

	memset(&_settings, 0, sizeof(_settings));
	_settings.on_status           = on_status_callback;
	_settings.on_header_field     = on_header_field_callback;
	_settings.on_header_value     = on_header_value_callback;
	_settings.on_headers_complete = on_headers_complete_callback;
	_settings.on_message_complete = on_message_complete_callback;
	_settings.on_body             = on_body_callback;
}

bool HttpClient::DoRedirect()
{
	if (_response.Success && _response.StatusCode == 302 &&
	    !_response.LocationHeader.empty() && _redirectDepth < 5)
	{
		_redirectDepth++;
		string location = _response.LocationHeader;

		if (!Uri::IsAbsolute(location))
		{
			location = _uri.Scheme + "://" + _uri.Host + location;
		}

		/* Call Get(Uri) directly — bypasses the string-wrapper _redirectDepth = 0
		   reset so the incremented depth survives into the next Request() call.
		   §redirect-fix: was Get(string) which reset depth to 0 via the wrapper,
		   making the < 5 check impossible to accumulate. */
		Uri redirectUri(location);
		if (!redirectUri.Valid)
			return false; /* malformed redirect Location — treat as no redirect */
		Get(redirectUri, _onComplete);
		return true;
	}
	return false;
}

string HttpClient::GetRemoteHost(const Uri& uri)
{
	if (_proxyHost != "")
		return _proxyHost;
	return uri.Host;
}

int HttpClient::GetRemotePort(const Uri& uri)
{
	if (_proxyHost != "" && _proxyPort > 0)
		return _proxyPort;
	/* An explicit port in the URL wins: an OpenAI-compatible server does not
	 * have to live on 443. */
	if (uri.Port > 0)
		return uri.Port;
	else if(uri.Scheme == "https")
		return 443;
	return 80;
}

bool HttpClient::Connect()
{
	DebugLog("Connect", "TCP open");
	OSErr err;
	unsigned long ipAddress;

	err = InitNetwork();
	if (err != noErr)
	{
		_response.ErrorCode = ConnectionError;
		_response.ErrorMsg = "InitNetwork returned " + to_string(err);
		return false;
	}

	string remoteHost = (_stunnelHost != "") ? _stunnelHost : GetRemoteHost(_uri);
	char* hostname = (char*)remoteHost.c_str();
	err = ResolveHost(remoteHost, &ipAddress); /* not hostname: see ResolveHost */
	if (err != noErr)
	{
		_response.ErrorCode = ConnectionError;
		_response.ErrorMsg = "ConvertStringToAddr returned " + to_string(err) + " for hostname " + string(hostname);
		return false;
	}

	err = tcp_create_stream(&_stream, BUF_SIZE, (GiveTimePtr)MacYield, (int*)&_cancel);
	if (err != noErr)
	{
		_response.ErrorCode = ConnectionError;
		_response.ErrorMsg = "CreateStream returned " + to_string(err);
		return false;
	}

	err = OpenConnection(_stream, ipAddress, _stunnelPort > 0 ? _stunnelPort : GetRemotePort(_uri), 0, (GiveTimePtr)MacYield, &_cancel);
	if (err == noErr) {
		if (_uri.Scheme == "https" && _proxyHost != "")
			Connect(_uri, _stream);
	}
	else
	{
		_response.ErrorCode = ConnectionError;
		_response.ErrorMsg = "OpenConnection returned " + to_string(err);
		return false;
	}

	return true;
}

bool HttpClient::Request()
{
	DebugLog("Request(send)", "SendData");
	OSErr err = SendData(
		_stream,
		(Ptr)_request.c_str(),
		(unsigned short)_request.size(), // §13 PERF: .size() is O(1); strlen is O(n)
		false,
		(GiveTimePtr)MacYield,
		&_cancel);

	if (err != noErr)
	{
		_response.ErrorCode = ConnectionError;
		_response.ErrorMsg = "SendData returned " + to_string(err);
		return false;
	}

	return true;
}

bool HttpClient::Response()
{
	DebugLog("Response", "RecvData loop");
	static unsigned char buf[BUF_SIZE]; // §3 MEMORY: static keeps 8 KB off stack; safe — non-reentrant plain-HTTP path
	unsigned short dataLength;
	int ret;

	while (true)
	{
		dataLength = sizeof(buf) - 1;

		OSErr err = RecvData(
			_stream,
			(Ptr)&buf, &dataLength,
			false,
			(GiveTimePtr)MacYield,
			&_cancel);

		if (err != noErr && err != connectionClosing)
		{
			_response.ErrorCode = ConnectionError;
			_response.ErrorMsg = "RecvData returned " + to_string((int)err);
			return false;
		}

		ret = http_parser_execute(&_parser, &_settings, (const char*)&buf, dataLength);

		if (_response.MessageComplete || err == connectionClosing)
		{
			_response.Success = true;
			break;
		}

		if (ret < 0)
		{
			_response.ErrorCode = ConnectionError;
			_response.ErrorMsg = "http_parser_execute returned " + to_string(ret);
			return false;
		}
	}

	return true;
}

void HttpClient::NetClose()
{
	DebugLog("NetClose", "TCP close");
	CloseConnection(_stream, (GiveTimePtr)MacYield, &_cancel);
	ReleaseStream(_stream, (GiveTimePtr)MacYield, &_cancel);

	if (!DoRedirect())
	{
		_status = Idle;
		_response.discardBody = false;
		if (!_cancel)
		{
			_onComplete(_response);
		}
		else
		{
			_cancel = false;
		}
	}
}

#ifdef SSL_ENABLED
/* ------------------------------------------------------------------ */
/* SslInitContexts — one-time setup: entropy, DRBG, cert parse        */
/* OPT-4: certs parsed once and kept alive; never re-parsed           */
/* Called from SslConnect on first use.                               */
/* ------------------------------------------------------------------ */
bool HttpClient::SslInitContexts()
{
	DebugLog("SslInitContexts", "entropy+certs");
	const char *pers = "HttpClient";
	int ret;

#ifdef MBEDTLS_DEBUG
	mbedtls_debug_set_threshold(_debugLevel);
#endif

	mbedtls_net_init(&_server_fd, (void*)(GiveTimePtr)MacYield);
	mbedtls_ssl_init(&_ssl);
	mbedtls_ssl_config_init(&_conf);
	mbedtls_x509_crt_init(&_cacert);
	mbedtls_ctr_drbg_init(&_ctr_drbg);
	mbedtls_entropy_init(&_entropy);

	/* mbedTLS's own entropy source in this build is TickCount() and a
	 * stack address, which an observer could guess. Add Time Manager
	 * jitter (mac_entropy.c) and the seed the last launch saved. */
	if (mbedtls_entropy_add_source(&_entropy, mac_entropy_poll, NULL,
		MAC_ENTROPY_THRESHOLD, MBEDTLS_ENTROPY_SOURCE_STRONG) != 0)
		DebugLog("SslInitContexts", "jitter source not added");
	{
		unsigned char seed[kRandomSeedLen];
		if (LoadRandomSeed(seed))
			mbedtls_entropy_update_manual(&_entropy, seed, sizeof(seed));
		mbedtls_platform_zeroize(seed, sizeof(seed));
	}

	if ((ret = mbedtls_ctr_drbg_seed(&_ctr_drbg, mbedtls_entropy_func, &_entropy,
		(const unsigned char *)pers, strlen(pers))) != 0)
	{
		_response.ErrorCode = SSLError;
		_response.ErrorMsg = "mbedtls_ctr_drbg_seed returned " + to_string(ret);
		return false;
	}
	SaveNextSeed();   /* never the same seed twice */

	/* OPT-4: parse certs once; never freed until app exit */
	ret = mbedtls_x509_crt_parse_der(&_cacert, kGlobalSignRootCA_DER, kGlobalSignRootCA_DER_len);
	if (ret != 0)
	{
		_response.ErrorCode = SSLError;
		_response.ErrorMsg = "mbedtls_x509_crt_parse_der returned " + to_string(ret);
		return false;
	}

	ret = mbedtls_x509_crt_parse_der(&_cacert, kGoogleWE1_DER, kGoogleWE1_DER_len);
	if (ret != 0)
	{
		_response.ErrorCode = SSLError;
		_response.ErrorMsg = "mbedtls_x509_crt_parse_der (WE1) returned " + to_string(ret);
		return false;
	}

	/* WE2 signs the *.googleapis.com endpoints (Gemini); WE1 signs the
	 * Cloudflare-fronted API hosts. Both are needed for the provider list. */
	ret = mbedtls_x509_crt_parse_der(&_cacert, kGoogleWE2_DER, kGoogleWE2_DER_len);
	if (ret != 0)
	{
		_response.ErrorCode = SSLError;
		_response.ErrorMsg = "mbedtls_x509_crt_parse_der (WE2) returned " + to_string(ret);
		return false;
	}

	/* OPT-3: load persistent ECDSA cache from prefs file into memory */
	uecc_load_persist_ecdsa_cache();

	/* Shamir2: if server cert pub (slot 1) was saved from a previous boot,
	 * precompute the Shamir2 table now so the first handshake uses the fast path. */
	uecc_precompute_shamir2_table();

	/* OPT-2: load cached DNS IP */
	{
		CachedDNS dns;
		LoadDNSCache(dns);
		/* Valid if less than 6 hours old (wall clock: survives reboots;
		 * an entry from an older build holds ticks and reads as stale) */
		unsigned long now; GetDateTime(&now);
		if (dns.ip != 0 && now >= dns.secs_written &&
			(now - dns.secs_written) < 6UL * 3600UL)
		{
			_cachedIpAddress = dns.ip;
		}
	}

	/* OPT-5: restore session ticket from previous run so first handshake
	 * after app restart uses resumption (~1s) instead of full SKE+donna (~13s). */
	LoadSessionFromDisk();

	/* OPT-7: precompute X25519 keypair now so TcpConnect can start TCP
	 * immediately without burning Cloudflare's handshake timeout on keygen.
	 * DRBG is already seeded above; yield hook keeps MacTCP alive. */
	uECC_set_yield(MacYield);
	fast_alloc_set_yield(MacYield);
	if (!uecc_x25519_pre_ready())
		uecc_precompute_x25519_key(mbedtls_ctr_drbg_random, &_ctr_drbg);
	uECC_set_yield(0);
	fast_alloc_set_yield(0);

	_sslInited = true;
	return true;
}

/* ------------------------------------------------------------------ */
/* SaveNextSeed — 32 fresh bytes for the next launch to mix in, so    */
/* every launch builds on the jitter of the ones before it.           */
/* FreshenRandom — before each later X25519 key: fold in the timing    */
/* notes gathered since (cheap, credited nothing) and save a new seed. */
/* A full reseed would cost 0.6-0.7 s of interrupt gathering after     */
/* every page; the seeding in SslInitContexts already holds 128 bits.  */
/* ------------------------------------------------------------------ */
void HttpClient::SaveNextSeed()
{
	unsigned char seed[kRandomSeedLen];
	if (mbedtls_ctr_drbg_random(&_ctr_drbg, seed, sizeof(seed)) == 0)
		SaveRandomSeed(seed);
	mbedtls_platform_zeroize(seed, sizeof(seed));
}

void HttpClient::FreshenRandom()
{
	unsigned char fresh[32];
	mac_entropy_take_notes(fresh);
	mbedtls_ctr_drbg_update(&_ctr_drbg, fresh, sizeof(fresh));
	mbedtls_platform_zeroize(fresh, sizeof(fresh));
	SaveNextSeed();
}

/* ------------------------------------------------------------------ */
/* SslConfigSetup — configure _conf and _ssl for a new connection.    */
/* Called once for Uninitialized → Connecting.                        */
/* ------------------------------------------------------------------ */
bool HttpClient::SslConfigSetup()
{
	int ret;

	if ((ret = mbedtls_ssl_config_defaults(&_conf,
		MBEDTLS_SSL_IS_CLIENT,
		MBEDTLS_SSL_TRANSPORT_STREAM,
		MBEDTLS_SSL_PRESET_DEFAULT)) != 0)
	{
		_response.ErrorCode = SSLError;
		_response.ErrorMsg = "mbedtls_ssl_config_defaults returned " + to_string(ret);
		return false;
	}

	/* Verification is on unless the application turned it off. It is only off
	 * for servers this build has no anchor for (an arbitrary OpenAI-compatible
	 * host); the trust store holds WE1 and WE2 and nothing else, so leaving it
	 * required means "cannot connect", not "connects insecurely". */
	mbedtls_ssl_conf_authmode(&_conf, sVerifyPeer
	                                        ? MBEDTLS_SSL_VERIFY_REQUIRED
	                                        : MBEDTLS_SSL_VERIFY_NONE);
	mbedtls_ssl_conf_ca_chain(&_conf, &_cacert, NULL);
	mbedtls_ssl_conf_rng(&_conf, mbedtls_ctr_drbg_random, &_ctr_drbg);
	mbedtls_ssl_conf_extended_master_secret(&_conf, MBEDTLS_SSL_EXTENDED_MS_DISABLED);
	mbedtls_ssl_conf_session_tickets(&_conf, MBEDTLS_SSL_SESSION_TICKETS_ENABLED);
	mbedtls_ssl_conf_max_tls_version(&_conf, MBEDTLS_SSL_VERSION_TLS1_2);
	mbedtls_ssl_conf_min_tls_version(&_conf, MBEDTLS_SSL_VERSION_TLS1_2);

	/* X25519 first: Montgomery ladder + fast_alloc pool makes compute_shared
	 * ~5s on Classic II (68020 @ 8MHz) vs ~13s for P-256 micro-ecc (which
	 * can't use precomputed tables for arbitrary server pubkeys).
	 * P-256 kept as fallback for servers that don't support X25519. */
	static const mbedtls_ecp_group_id prefer_x25519[] = {
		MBEDTLS_ECP_DP_CURVE25519,
		MBEDTLS_ECP_DP_SECP256R1,
		MBEDTLS_ECP_DP_SECP384R1,
		MBEDTLS_ECP_DP_NONE
	};
	mbedtls_ssl_conf_curves(&_conf, prefer_x25519);

#ifdef MBEDTLS_DEBUG
	mbedtls_ssl_conf_dbg(&_conf, ssl_debug, stdout);
#endif

	if (_overrideCipherSuite[0] > 0)
		mbedtls_ssl_conf_ciphersuites(&_conf, _overrideCipherSuite);
	else
		mbedtls_ssl_conf_ciphersuites(&_conf, _cipherSuites);

	if ((ret = mbedtls_ssl_setup(&_ssl, &_conf)) != 0)
	{
		_response.ErrorCode = SSLError;
		_response.ErrorMsg = "mbedtls_ssl_setup returned " + to_string(ret);
		DebugLog("SslCfgSetup", _response.ErrorMsg.c_str());
		return false;
	}

	/* Session resumption (mbedtls_ssl_set_session) is called by the Dead path
	 * after SslConfigSetup returns, not inside here, so callers can inspect
	 * the return value and log before deciding whether to arm resumption. */

	if ((ret = mbedtls_ssl_set_hostname(&_ssl, _uri.Host.c_str())) != 0)
	{
		_response.ErrorCode = SSLError;
		_response.ErrorMsg = "mbedtls_ssl_set_hostname returned " + to_string(ret);
		return false;
	}

	mbedtls_ssl_set_bio(&_ssl, &_server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);
	return true;
}

/* ------------------------------------------------------------------ */
/* ResolveAndCreateStream — shared DNS + CreateStream for both paths  */
/* ------------------------------------------------------------------ */
bool HttpClient::ResolveAndCreateStream() // §10 DEDUP: extracted from TcpConnect/TcpReconnect
{
	/* OPT-2: use cached IP if available, else resolve */
	if (_cachedIpAddress == 0) {
		string host = GetRemoteHost(_uri);
		OSErr dnsErr = ResolveHost(host, &_cachedIpAddress); /* see ResolveHost */
		if (dnsErr != noErr || _cachedIpAddress == 0) {
			_response.ErrorCode = ConnectionError;
			_response.ErrorMsg = "ConvertStringToAddr returned " + to_string(dnsErr);
			DebugLog("ResolvStream", _response.ErrorMsg.c_str());
			return false;
		}
		_dnsNeedsSave = true; /* flush to disk later in SslClose (OPT-2) */
	}

	unsigned long stream;
	OSErr err = tcp_create_stream(&stream, BUF_SIZE, (GiveTimePtr)MacYield, (int*)&_cancel);
	if (err != noErr)
	{
		_response.ErrorCode = ConnectionError;
		_response.ErrorMsg = "CreateStream returned " + to_string(err);
		DebugLog("ResolvStream", _response.ErrorMsg.c_str());
		return false;
	}
	_server_fd.fd = (int)stream;
	_server_fd.give_time_ptr = (void*)(GiveTimePtr)MacYield;
	_server_fd.terminated = 0; /* clear any stale flag from a previous failed TcpConnect retry */
	return true;
}

/* ------------------------------------------------------------------ */
/* TcpConnect — DNS (or cached) + CreateStream + async open (OPT-6)   */
/* ------------------------------------------------------------------ */
bool HttpClient::TcpConnect()
{
	DebugLog("TcpConnect", "async open");
	OSErr err;

	err = InitNetwork();
	if (err != noErr)
	{
		_response.ErrorCode = ConnectionError;
		_response.ErrorMsg = "InitNetwork returned " + to_string(err);
		return false;
	}

	if (!ResolveAndCreateStream()) // §10 DEDUP
		return false;

	/* OPT-6: fire TCPActiveOpen async. Keygen is already done in SslInitContexts,
	 * so TCP can proceed immediately and TLS starts as soon as TCP connects. */
	tcp_open_async((unsigned long)_server_fd.fd, _cachedIpAddress, (short)GetRemotePort(_uri));

	err = tcp_wait_connected((GiveTimePtr)MacYield, &_cancel);
	if (err != noErr)
	{
		_response.ErrorCode = ConnectionError;
		_response.ErrorMsg = "tcp_wait_connected returned " + to_string((int)err)
		                   + " (MacTCP err=" + to_string((int)gLastMacTCPErr) + ")";
		DebugLog("TcpConnect", _response.ErrorMsg.c_str());
		/* The async TCPActiveOpen completed with an error — connection was
		 * never established.  Mark terminated so TcpReconnect's mbedtls_net_free
		 * skips CloseConnection (TCPClose on a non-established stream can crash). */
		_server_fd.terminated = 1;
		return false;
	}

	return true;
}

/* ------------------------------------------------------------------ */
/* TcpReconnect — Dead path: release old stream + reconnect           */
/* Keypairs are already precomputed by SslClose.                      */
/* ------------------------------------------------------------------ */
bool HttpClient::TcpReconnect()
{
	DebugLog("TcpReconnect", "dead->new stream");
	/* Release the dead stream */
	if (_server_fd.fd >= 0) {
		mbedtls_net_free(&_server_fd);
		_server_fd.fd = -1;
	}

	/* Re-resolve DNS if needed, then create a fresh stream */
	if (!ResolveAndCreateStream()) // §10 DEDUP
		return false;

	/* Keypairs are precomputed (done in SslClose); keys are ready.
	 * Use blocking OpenConnection for the simpler Dead reconnect path.
	 * timeout=30: commandTimeoutValue=0 means "no timeout" in MacTCP — infinite hang. */
	OSErr err = OpenConnection((unsigned long)_server_fd.fd, _cachedIpAddress,
	                           (short)GetRemotePort(_uri),
	                           30, (GiveTimePtr)MacYield, &_cancel);
	if (err != noErr)
	{
		_response.ErrorCode = ConnectionError;
		_response.ErrorMsg = "OpenConnection returned " + to_string(err);
		DebugLog("TcpReconnect", _response.ErrorMsg.c_str());
		_cachedIpAddress = 0;  /* force re-resolve on next attempt; IP may have rotated */
		_dnsNeedsSave = false;
		/* Mark as terminated so the next mbedtls_net_free skips CloseConnection.
		 * TCPClose on a stream where OpenConnection never completed can crash,
		 * same as the non-established case in TcpConnect. */
		_server_fd.terminated = 1;
		return false;
	}

	return true;
}

/* ------------------------------------------------------------------ */
/* TcpConnectionAlive — health check via TCPStatus (OPT-1)            */
/* ------------------------------------------------------------------ */
bool HttpClient::TcpConnectionAlive()
{
	if (_server_fd.fd < 0)
		return false;
	short state = tcp_connection_state((unsigned long)_server_fd.fd);
	return (state == 8); /* 8 = TCPS_ESTABLISHED */
}

/* ------------------------------------------------------------------ */
/* IsSessionExpired — true if saved session is older than 23 hours    */
/* Checks both wall-clock age (survives app restart via prefs          */
/* saved_datetime) and in-session TickCount age. Cloudflare rotates    */
/* ticket keys ~daily; 23 h matches the established threshold and sits */
/* above IdleTicketRefresh's 20 h proactive refresh.                   */
/* ------------------------------------------------------------------ */
bool HttpClient::IsSessionExpired()
{
	if (_ticketSavedDateTime != 0) {
		/* Unsigned subtraction also catches a clock set backwards:
		 * the wrap yields a huge age → expired → safe full handshake. */
		unsigned long now;
		GetDateTime(&now);
		if (now - _ticketSavedDateTime > 23UL * 3600UL)
			return true;
	}
	if (_sessionEstablishedTicks == 0) return false;
	return (TickCount() - _sessionEstablishedTicks) > (23UL * 3600UL * 60UL);
}

/* ------------------------------------------------------------------ */
/* ArmResumeIfFresh — offer the saved ticket unless locally stale     */
/* Offering an expired ticket costs a doomed resume attempt + TCP     */
/* reconnect (~7 s on Classic II at cold start); drop it here and go  */
/* straight to a full handshake instead. The ResumeRejected() path in */
/* SslHandshake remains the authoritative fallback for early key     */
/* rotation or clock skew.                                            */
/* Call only after SslConfigSetup(): ssl_setup must have allocated    */
/* session_negotiate before mbedtls_ssl_set_session is legal.         */
/* ------------------------------------------------------------------ */
void HttpClient::ArmResumeIfFresh()
{
	if (!_hasSavedSession) return;
	if (IsSessionExpired()) {
		DebugLog("SslConnect", "ticket expired, full hs");
		mbedtls_ssl_session_free(&_savedSession);
		mbedtls_ssl_session_init(&_savedSession);
		_hasSavedSession = false;
		return;
	}
	/* On success the server skips Certificate/SKE/ECDHE entirely — saves ~7.5s. */
	int sret = mbedtls_ssl_set_session(&_ssl, &_savedSession);
	DebugLog("SslConnect", sret == 0 ? "resume armed" : "resume skip");
}

/* ------------------------------------------------------------------ */
/* SslConnect — persistent connection logic (OPT-1)                   */
/*                                                                     */
/* Idle:        health check. If alive, return true (reuse).          */
/*              If dead, fall through to Dead path.                   */
/* Uninit:      one-time contexts + TCP connect + SSL config.         */
/* Connecting:  (should not be seen here, handled in HttpsRequest)    */
/* Dead:        release stream + TCP reconnect + SSL session reset.   */
/* ------------------------------------------------------------------ */
bool HttpClient::SslConnect()
{
	DebugLog("SslConnect", "start");
	if (_connState == kConnState_Idle) {
		/* Idle-age check: Cloudflare drops connections that never served a
		 * request within a few seconds of the handshake (observed dead at
		 * ~6 s idle on hardware), and honours at most our requested 90 s
		 * keep-alive after a request. Probing a half-closed stream costs
		 * ~2 s (ssl_write + ssl_read to failure) plus a parser reset, so
		 * when the connection is past its plausible lifetime, skip the
		 * probe and reconnect directly. TCPStatus can't catch this: it
		 * reports ESTABLISHED until the server's FIN arrives.            */
		long idleLimit = _connServedRequest ? (90L * 60L)  /* Keep-Alive: timeout=90 */
		                                    : (5L * 60L);  /* warm conn, no request yet */
		if (_lastUseTicks != 0 && (TickCount() - _lastUseTicks) > idleLimit) {
			DebugLog("SslConnect", "idle too long, reconnect");
			/* Presumed half-closed: terminate so mbedtls_net_free skips
			 * TCPClose, which can block or crash on a half-closed
			 * Cloudflare stream (same pattern as ForceReconnect).      */
			_server_fd.terminated = 1;
			_connState = kConnState_Dead;
		} else if (TcpConnectionAlive()) {
			return true;    /* true keep-alive: no handshake needed */
		} else {
			_connState = kConnState_Dead;
		}
	}

	/* One-time initialization (OPT-4: certs parsed once here) */
	if (!_sslInited) {
		if (!SslInitContexts()) return false;
	}

	if (_connState == kConnState_Uninitialized) {
		/* First connection after init.  SslConfigSetup calls ssl_setup which
		 * allocates session_negotiate; after that set_session is safe.
		 * LoadSessionFromDisk (called in SslInitContexts) may have populated
		 * _savedSession from a previous run — offer the ticket on first connect. */
		if (!SslConfigSetup())  return false;
		ArmResumeIfFresh();
		if (!TcpConnect())      return false;
		_connServedRequest = false; /* fresh TCP stream — no request served yet */
		_connState = kConnState_Connecting;
		return true;
	}

	/* kConnState_Dead: reconnect path */

	/* Keypairs should be precomputed by SslClose; these are fallback calls
	 * for the rare case they aren't ready (e.g. first Dead after WarmUp). */
	if (!uecc_x25519_pre_ready()) {
		/* Should not fire any more: WarmUp and HttpsRequest both replenish.
		 * If it appears in a log, a consume path is missing its replenish. */
		DebugLog("SslConnect", "x25519 fallback keygen");
		uECC_set_yield(MacYield);
		fast_alloc_set_yield(MacYield);
		FreshenRandom();
		uecc_precompute_x25519_key(mbedtls_ctr_drbg_random, &_ctr_drbg);
		uECC_set_yield(0);
		fast_alloc_set_yield(0);
	}

	if (!TcpReconnect()) return false;

	/* Re-init SSL context and fully reconfigure _conf for a clean handshake.
	 * Calling SslConfigSetup() (rather than bare mbedtls_ssl_setup) ensures
	 * _conf has valid TLS version / cipher / RNG settings even when the
	 * Uninitialized path's TcpConnect failed early (before _conf was fully
	 * applied to _ssl) or when _conf is in any unexpected state.
	 * SslConfigSetup also sets hostname and bio, so no separate calls needed. */
	mbedtls_ssl_free(&_ssl);
	mbedtls_ssl_init(&_ssl);
	if (!SslConfigSetup()) {
		return false;   /* SslConfigSetup already set _response.ErrorMsg */
	}

	/* Session resumption: restore saved ticket so mbedtls includes it in
	 * ClientHello.  ssl_setup (called inside SslConfigSetup) allocates
	 * session_negotiate, so set_session is safe to call here.            */
	ArmResumeIfFresh();

	_connServedRequest = false; /* fresh TCP stream — no request served yet */
	_connState = kConnState_Connecting;
	return true;
}

bool HttpClient::SslHandshake()
{
	DebugLog("SslHandshak", "mbedtls handshake");
	long ticksBefore = TickCount();
	int ret = mbedtls_ssl_handshake(&_ssl);
	long ticksElapsed = TickCount() - ticksBefore;

	if (ret == 0) {
		_lastHandshakeOk = true;
		_ticketDirty     = true; /* new or resumed handshake — ticket may have changed */
		char ok[40]; snprintf(ok, sizeof(ok), "ok %lds (ticks=%ld)",
		                      ticksElapsed / 60, ticksElapsed);
		DebugLog("SslHandshak", ok);
		DebugLog("SslHandshak", mbedtls_ssl_get_ciphersuite(&_ssl)); /* OPT #9: confirm ChaCha negotiated */
		return true;
	}

	_lastHandshakeOk = false;
	_connState = kConnState_Dead; /* failed handshake → force reconnect next time */

	/* If we offered a session ticket and the server rejected it very fast
	 * (st=2, ticks<60 = ~1s), the ticket is stale (e.g. Cloudflare key rotation).
	 * Clear it so the next attempt does a full handshake instead of looping.
	 * This is expected behaviour on first connect after app restart — not an error. */
	if (_hasSavedSession && ticksElapsed < 60) {
		DebugLog("SslHandshak", "saved session expired, reconnecting");
		mbedtls_ssl_session_free(&_savedSession);
		mbedtls_ssl_session_init(&_savedSession);
		_hasSavedSession = false;
		_resumeRejected  = true;   /* signal callers: retry once, full handshake */
		_response.ErrorCode = SSLError;
		_response.ErrorMsg  = "resume rejected";
		return false; /* skip verbose error log — this path is normal */
	}

	int state = (int)_ssl.MBEDTLS_PRIVATE(state);

	if (ret == MBEDTLS_ERR_NET_RECV_FAILED)
	{
		_response.ErrorCode = ConnectionTimeout;
		_response.ErrorMsg = "mbedtls_ssl_handshake returned timeout " + to_string(ret)
		                   + " at state " + to_string(state)
		                   + " recvErr=" + to_string((int)gLastRecvErr)
		                   + " ticks=" + to_string((int)ticksElapsed);
	}
	else
	{
		char vrfy_buf[256] = "";
		uint32_t flags = mbedtls_ssl_get_verify_result(&_ssl);
		if (flags != 0)
			mbedtls_x509_crt_verify_info(vrfy_buf, sizeof(vrfy_buf), " ", flags);
		int alertLevel = (int)_ssl.MBEDTLS_PRIVATE(in_msg)[0];
		int alert      = (int)_ssl.MBEDTLS_PRIVATE(in_msg)[1];
		int msgtype    = (int)_ssl.MBEDTLS_PRIVATE(in_msgtype);
		int msglen     = (int)_ssl.MBEDTLS_PRIVATE(in_msglen);
		_response.ErrorCode = SSLError;
		const char *cs = mbedtls_ssl_get_ciphersuite(&_ssl);
		_response.ErrorMsg = "mbedtls_ssl_handshake returned error " + to_string(ret)
		                   + " at state " + to_string(state)
		                   + " cs=" + string(cs ? cs : "?")
		                   + " grp=" + to_string(g_last_ecdh_grp_id)
		                   + " recvErr=" + to_string((int)gLastRecvErr)
		                   + " ncopy=" + to_string((int)gLastAmtUnread)
		                   + " ticks=" + to_string((int)ticksElapsed)
		                   + " ppeak=" + to_string((int)g_pool_peak)
		                   + " pover=" + to_string((int)g_pool_overflow_count)
		                   + vrfy_buf;
	}
	/* Log compact summary first (fits in 53-char DebugLog body), then full msg. */
	{ char _s[54];
	  snprintf(_s, sizeof(_s), "err=%d st=%d grp=%d tk=%ld re=%d nc=%d",
	           ret, state, g_last_ecdh_grp_id, ticksElapsed,
	           (int)gLastRecvErr, (int)gLastAmtUnread);
	  DebugLog("SslHandshake",_s); }
	DebugLog("SslHandshake",_response.ErrorMsg.c_str());
	return false;
}

bool HttpClient::SslVerifyCert()
{
	/* Session resumption: server does not re-send its certificate.
	 * The cert was already verified during the full handshake that
	 * produced the session ticket; trust that result here. */
	if (mbedtls_ssl_get_peer_cert(&_ssl) == NULL)
		return true;

	DebugLog("SslVerifyCert", "cert check");
	uint32_t flags = mbedtls_ssl_get_verify_result(&_ssl);
	if (flags != 0)
	{
		char vrfy_buf[256];
		mbedtls_x509_crt_verify_info(vrfy_buf, sizeof(vrfy_buf), "", flags);
		_response.ErrorCode = SSLError;
		_response.ErrorMsg = string("Certificate verification failed: ") + vrfy_buf;
		return false;
	}
	return true;
}

bool HttpClient::SslRequest()
{
	DebugLog("SslRequest", "ssl_write");
	if (_cRequest == NULL)
		_cRequest = _request.c_str();

	size_t remaining = _request.size() - (size_t)(_cRequest - _request.c_str());
	while (remaining > 0)
	{
		int ret = mbedtls_ssl_write(&_ssl, (const unsigned char*)_cRequest, remaining);

		if (ret > 0) {
			/* Advance past the bytes that were sent; loop for the rest */
			_cRequest += ret;
			remaining -= (size_t)ret;
			continue;
		}

		if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY ||
		    ret == MBEDTLS_ERR_NET_CONN_RESET)
		{
			_connState = kConnState_Dead;
			_response.ErrorCode = ConnectionError;
			_response.ErrorMsg = "mbedtls_ssl_write: peer closed connection " + to_string(ret);
			return false;
		}

		if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
		{
			_connState = kConnState_Dead;
			_response.ErrorCode = ConnectionError;
			_response.ErrorMsg = "mbedtls_ssl_write returned " + to_string(ret);
			return false;
		}
		/* WANT_READ or WANT_WRITE: retry with the same buffer position */
	}

	return true;
}

bool HttpClient::SslResponse()
{
	DebugLog("SslResponse", "ssl_read loop");
	/* Static: keeps 8 KB off the main-thread stack so the settings-dialog
	 * call chain (which is ~6 frames deeper than pageRequest) does not
	 * overflow the 64 KB stack.  Safe because SslResponse is never
	 * re-entered: the cooperative save thread is always drained before
	 * FetchNotionPageList calls Post, and SslResponse itself is not
	 * recursive.                                                         */
	static unsigned char buf[BUF_SIZE];
	int len;

	while (true)
	{
		len = sizeof(buf);
		int sslRet = mbedtls_ssl_read(&_ssl, buf, len);

		if (sslRet == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || sslRet == 0)
		{
			_response.Success = _response.MessageComplete;
			_connState = kConnState_Dead; /* peer closed TLS session */
			break;
		}
		if (sslRet == MBEDTLS_ERR_NET_CONN_RESET)
		{
			_connState = kConnState_Dead;
			_response.ErrorCode = ConnectionError;
			_response.ErrorMsg = "mbedtls_ssl_read: connection reset";
			return false;
		}
		if (sslRet == MBEDTLS_ERR_SSL_WANT_READ || sslRet == MBEDTLS_ERR_SSL_WANT_WRITE)
		{
			continue;
		}
		if (sslRet < 0)
		{
			_connState = kConnState_Dead;
			_response.ErrorCode = ConnectionError;
			_response.ErrorMsg = "mbedtls_ssl_read returned " + to_string(sslRet);
			return false;
		}

		int parsed = (int)http_parser_execute(&_parser, &_settings, (const char*)buf, (size_t)sslRet);

		if (_response.MessageComplete)
		{
			_response.Success = true;
			/* Body was skipped via on_headers_complete: unread data remains in
			 * the TCP stream, so the connection cannot be reused. */
			if (_response.skipBody) {
				_connState = kConnState_Dead;
			}
			/* OPT-1: detect Connection: close and mark stream for teardown */
			else if (_response.ConnectionHeader.find("close") != string::npos)
			{
				_connState = kConnState_Dead;
			}
			break;
		}

		if (parsed < 0)
		{
			_connState = kConnState_Dead;
			_response.ErrorCode = ConnectionError;
			_response.ErrorMsg = "http_parser_execute returned " + to_string(parsed);
			return false;
		}
	}

	return true;
}

/* ------------------------------------------------------------------ */
/* SaveSessionToDisk / LoadSessionFromDisk — OPT-5                    */
/* ------------------------------------------------------------------ */

void HttpClient::SaveSessionToDisk()
{
	if (!_hasSavedSession) return;

	/* mbedtls_ssl_session_save captures EVERY field resume needs (id, start,
	 * negotiated extensions, peer cert, ticket...).  The previous hand-rolled
	 * master+ticket+ciphersuite copy reconstructed an incomplete session and
	 * every cross-restart resume was rejected (~10s wasted per cold start).
	 * ~2 KB struct: malloc'd, not stack — thread stacks are small.          */
	PersistedTLSSession *p = (PersistedTLSSession *)malloc(sizeof(PersistedTLSSession));
	if (!p) return;
	memset(p, 0, sizeof(*p));

	size_t olen = 0;
	int ret = mbedtls_ssl_session_save(&_savedSession, p->blob, kTLSBlobMax, &olen);
	if (ret != 0 || olen == 0 || olen > kTLSBlobMax) {
		char m[44];
		snprintf(m, sizeof(m), "save serialize fail -0x%04X len=%u", (unsigned)-ret, (unsigned)olen);
		DebugLog("Ticket", m);
		free(p);
		return;
	}

	p->magic    = 0x544C5353UL; /* 'TLSS' */
	p->blob_len = (unsigned short)olen;
	GetDateTime(&p->saved_datetime);
	_ticketSavedDateTime = p->saved_datetime; /* for TicketAgeSeconds() */

	SaveTLSSession(*p);
	free(p);
}

long HttpClient::TicketAgeSeconds()
{
	if (!_hasSavedSession || _ticketSavedDateTime == 0) return -1;
	unsigned long now;
	GetDateTime(&now);
	return (long)(now - _ticketSavedDateTime);
}

void HttpClient::LoadSessionFromDisk()
{
	PersistedTLSSession *p = (PersistedTLSSession *)malloc(sizeof(PersistedTLSSession));
	if (!p) return;
	if (!LoadTLSSession(*p)) { free(p); return; }

	mbedtls_ssl_session_free(&_savedSession);
	mbedtls_ssl_session_init(&_savedSession);
	int ret = mbedtls_ssl_session_load(&_savedSession, p->blob, p->blob_len);
	if (ret != 0) {
		char m[44];
		snprintf(m, sizeof(m), "load deserialize fail -0x%04X", (unsigned)-ret);
		DebugLog("Ticket", m);
		mbedtls_ssl_session_free(&_savedSession);
		mbedtls_ssl_session_init(&_savedSession);
		free(p);
		return;
	}
	_hasSavedSession         = true;
	_sessionEstablishedTicks = TickCount(); /* IsSessionExpired not used cross-restart */
	_ticketSavedDateTime     = p->saved_datetime; /* for TicketAgeSeconds() */
	DebugLog("SslInit", "ticket loaded from disk");
	free(p);
}

/* ------------------------------------------------------------------ */
/* SslClose — lightweight end-of-request cleanup (OPT-1)              */
/* Does NOT tear down TCP or SSL structures.                          */
/* ------------------------------------------------------------------ */
void HttpClient::SslClose()
{
	/* Save session ticket only when the handshake was fresh (_ticketDirty) and
	 * the response body was fully consumed.  Guards:
	 *   _lastHandshakeOk — failed handshakes must not persist a stale ticket
	 *   _ticketDirty     — avoids redundant disk writes on every keep-alive request
	 *   skipBody         — abandoned mid-stream connections may have a tainted ticket */
	if (!_response.skipBody && _lastHandshakeOk && _ticketDirty) {
		DebugLog("SslClose", "save session");
		mbedtls_ssl_session_free(&_savedSession);
		mbedtls_ssl_session_init(&_savedSession);
		if (mbedtls_ssl_get_session(&_ssl, &_savedSession) == 0) {
			_hasSavedSession = true;
			_sessionEstablishedTicks = TickCount();
			SaveSessionToDisk(); /* OPT-5: persist so next restart skips cold handshake */
			_ticketDirty = false;
		}
	} else if (!_lastHandshakeOk) {
		_hasSavedSession = false;
	}

	/* OPT-2: flush DNS cache if we resolved a new IP this session */
	if (_dnsNeedsSave && _cachedIpAddress != 0) {
		CachedDNS dns;
		dns.ip = _cachedIpAddress;
		GetDateTime(&dns.secs_written);
		SaveDNSCache(dns);
		_dnsNeedsSave = false;
	}

	/* OPT-3: flush persistent ECDSA cache if updated this session */
	if (uecc_persist_ecdsa_dirty()) {
		uecc_save_persist_ecdsa_cache();
		/* Shamir2: slot 1 (server cert pub) may have been newly populated.
		 * Precompute the table now so the next handshake uses the fast path. */
		uecc_precompute_shamir2_table();
	}

	/* OPT-1: leave connection alive if still healthy */
	if (_connState != kConnState_Dead) {
		_connState = kConnState_Idle;
		_lastUseTicks = TickCount();
		_connServedRequest = true; /* request completed → server honours Keep-Alive: timeout=90 */
	}

	/* Deliver response BEFORE precompute so the UI updates immediately.
	 * A nested request triggered by _onComplete may already precompute
	 * via its own SslClose; the guard below skips it in that case.    */
	if (!DoRedirect()) {
		_status = Idle;
		_response.discardBody = false; /* clear before callback so nested Patch/PatchWithBody
		                                  calls start fresh and are not tainted by outer Put() */
		if (!_cancel)
			_onComplete(_response);
		else
			_cancel = false;
	}

	/* Precompute keypairs for the next cold or Dead reconnect.
	 * Runs after _onComplete so the user sees their data first.
	 * Guard: skip if a nested SslClose already precomputed the keys. */
	if (!uecc_x25519_pre_ready()) {
		uECC_set_yield(MacYield);
		fast_alloc_set_yield(MacYield);
		FreshenRandom();
		uecc_precompute_x25519_key(mbedtls_ctr_drbg_random, &_ctr_drbg);
		uECC_set_yield(0);
		fast_alloc_set_yield(0);
	}
}

/* ------------------------------------------------------------------ */
/* SslFreeAll — full teardown; call only on app exit or hard reset     */
/* ------------------------------------------------------------------ */
void HttpClient::SslFreeAll()
{
	DebugLog("SslFreeAll", "free all TLS");
	if (!_sslInited) return;

	if (_connState != kConnState_Dead && _server_fd.fd >= 0 &&
	    mbedtls_ssl_is_handshake_over(&_ssl))
		mbedtls_ssl_close_notify(&_ssl);

	mbedtls_net_free(&_server_fd);
	mbedtls_x509_crt_free(&_cacert);
	/* _savedSession is intentionally preserved across SslFreeAll so that the
	 * next SslConnect (Uninitialized path) can attempt session resumption and
	 * skip the 2s ECC keygen.  SslClose() refreshes it after each request;
	 * IsSessionExpired() drops it after 23 h.  mbedTLS falls back to a full
	 * handshake automatically if the server rejects the ticket. */
	mbedtls_ssl_free(&_ssl);
	mbedtls_ssl_config_free(&_conf);
	mbedtls_ctr_drbg_free(&_ctr_drbg);
	mbedtls_entropy_free(&_entropy);

	_sslInited = false;
	_connState = kConnState_Uninitialized;
	/* _hasSavedSession and _sessionEstablishedTicks left intact */
}

/* Force connection to Dead so the next request opens a fresh TCP stream.
   Uses the lightweight Dead path (TcpReconnect + ssl_session_reset) rather
   than SslFreeAll, so SSL contexts stay alive and the session ticket is
   reused for an abbreviated handshake (~0.8s vs ~1.8s cold start).
   SslFreeAll is reserved for app exit (see ExitToShell call site). */
void HttpClient::ForceReconnect()
{
#ifdef SSL_ENABLED
	/* Nothing to tear down before first successful init or config. */
	if (!_sslInited || _connState == kConnState_Uninitialized) return;

	DebugLog("ForceReconn", "dead");
	/* Terminate so mbedtls_net_free skips CloseConnection — can block or
	   crash on a half-closed Cloudflare stream. */
	_server_fd.terminated = 1;
	if (_server_fd.fd >= 0) {
		mbedtls_net_free(&_server_fd);
		_server_fd.fd = -1;
	}
	/* Dead: next SslConnect() uses TcpReconnect + mbedtls_ssl_session_reset,
	   keeping entropy/DRBG/certs/conf alive.  Session ticket preserved for
	   abbreviated handshake; mbedTLS falls back to full if rejected. */
	_connState = kConnState_Dead;
#endif
}

#endif // SSL_ENABLED

static int on_body_callback(http_parser* parser, const char *at, size_t length)
{
	HttpResponse* response = (HttpResponse*)parser->data;
	if (!response->bodyLogged) { DebugLog("on_body", "first chunk"); response->bodyLogged = true; } // §15 ARCH
	if (response->onChunk)
		response->onChunk(at, (int)length);
	else if (!response->discardBody) /* OPT-8: skip accumulation for PATCH responses */
		response->Content.append(at, length);
	return 0;
}

static int on_header_field_callback(http_parser* parser, const char *at, size_t length)
{
	HttpResponse* response = (HttpResponse*)parser->data;
#ifdef DEBUG
	DebugLog("on_hdr_field", string(at, length).c_str()); // §14 PERF: heap alloc per field — debug only
#endif
	/* assign(at, length) reuses CurrentHeader's existing heap buffer across
	 * headers instead of allocating a new string each call.             */
	response->CurrentHeader.assign(at, length);
	return 0;
}

static int on_header_value_callback(http_parser* parser, const char *at, size_t length)
{
	HttpResponse* response = (HttpResponse*)parser->data;
#ifdef DEBUG
	DebugLog("on_hdr_value", string(at, length).c_str()); // §14 PERF: heap alloc per value — debug only
#endif
	/* Store only the two headers we actually read; skip all others.
	 * http_parser already strips CRLF before invoking this callback. */
	const string& key = response->CurrentHeader;
	if (key == "Location") {
		response->LocationHeader.assign(at, length);
	} else if (key == "Connection") {
		response->ConnectionHeader.assign(at, length);
	} else if (key == "Content-Length") {
		/* `at` is not NUL-terminated — copy the bounded value into a small
		   stack buffer before strtol so it can't walk past `length`. */
		char numbuf[16];
		size_t nlen = length < sizeof(numbuf) - 1 ? length : sizeof(numbuf) - 1;
		memcpy(numbuf, at, nlen);
		numbuf[nlen] = '\0';
		char *endp;
		long n = strtol(numbuf, &endp, 10);
		if (endp != numbuf && n > 0 && n <= 524288)  /* cap at 512 KB — avoids OOM on low-heap Macs */
			response->Content.reserve((size_t)n);
	}
	return 0;
}

static int on_message_complete_callback(http_parser* parser)
{
	HttpResponse* response = (HttpResponse*)parser->data;
	DebugLog("on_msg_complete", "parse done");
	response->bodyLogged = false; /* §15 ARCH: reset so on_body logs first chunk of next request */
	response->MessageComplete = true;
	return 0;
}

static int on_headers_complete_callback(http_parser* parser)
{
	HttpResponse* response = (HttpResponse*)parser->data;
	DebugLog("on_hdrs_done", "headers parsed");
	/* For PATCH responses (discardBody=true) skip the body entirely.
	 * Returning 1 tells http_parser to call on_message_complete immediately
	 * without waiting for body bytes. The TCP stream still has body data
	 * pending, so the caller must close the connection (set Dead). */
	if (response->discardBody) {
		response->skipBody = true;
		return 1;
	}
	return 0;
}

static int on_status_callback(http_parser* parser, const char *at, size_t length)
{
	HttpResponse* response = (HttpResponse*)parser->data;
	{ char pbuf[16]; snprintf(pbuf, sizeof(pbuf), "HTTP %u", parser->status_code);
	  DebugLog("on_status", pbuf); }
	response->StatusCode = parser->status_code;
	return 0;
}

#ifdef MBEDTLS_DEBUG
static void ssl_debug(void *ctx, int level,
	const char *file, int line,
	const char *str)
{
	((void)level);

	FILE *fp;
	fp = fopen("Mac Volume:log.txt", "a");

	if (fp)
	{
		fprintf(fp, "%s:%04d: %s", file, line, str);
		fflush(fp);
		fclose(fp);
	}
}
#endif // MBEDTLS_DEBUG
