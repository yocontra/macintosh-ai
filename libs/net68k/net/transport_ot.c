/*
 * transport_ot.c — Open Transport transport for the net68k engine.
 *
 * This file replaces net_sockets_mac.c (kept beside it as
 * transport_mactcp.c) for Macs running Open Transport: System 7.5.3 with
 * OT 1.3 or later, which is every Mac OS 8/9 machine and every System 7
 * machine that installed OT. MacTCP is not present on Mac OS 9, and the
 * emulator harness runs there.
 *
 * It is a transport, not an abstraction: the same symbols, the same
 * semantics and the same call order as the MacTCP original, so that
 * HttpClient.cpp and everything above it compile and run unchanged.
 * Where the MacTCP version exposed MacTCPHelper (InitNetwork,
 * ConvertStringToAddr, OpenConnection, CloseConnection, SendData,
 * RecvData, ReleaseStream, AbortConnection) because HttpClient.cpp calls
 * those directly, this file provides them over OT too.
 *
 * HOW OT ASYNC WORKS
 *
 * OTOpenEndpoint gives us an endpoint; OTInstallNotifier registers a
 * callback OT runs at deferred-task time — no allocation, no Toolbox, no
 * handle dereferencing there. The notifier only sets flags, and the poll
 * loops below (which run at normal application time, yielding through
 * giveTime) read those flags and do the real work.
 *
 * The endpoint is bound *before* OTSetAsynchronous: OTBind on a
 * synchronous endpoint blocks until it has completed, whereas on an
 * asynchronous one it returns immediately and reports completion later
 * as T_BINDCOMPLETE, which nothing here waits for. OTConnect on an
 * unbound endpoint fails with kOTOutStateErr, intermittently, depending
 * on timing.
 *
 * Streams are identified by a small integer index, not a pointer:
 * mbedtls_net_context::fd is an `int`, which is 16 bits on m68k, so a
 * pointer would be truncated on its way into the engine. MacTCP stream
 * refs happened to survive that truncation; OT endpoint refs would not.
 */

#include <OpenTransport.h>
#include <OpenTptInternet.h>

#include <mbedtls/net_sockets.h>
#include <mbedtls/error.h>

#include <string.h>
#include <stdlib.h>

#include <Events.h>   /* TickCount, SystemTask */

/*
 * MacTCP's error numbers, from MacTCP.h. HttpClient.cpp compares against
 * these by name (it still includes MacTCP.h for the constants), so the
 * values have to agree with Apple's.
 */
#define connectionClosing    (-23005)
#define connectionTerminated (-23012)
#define commandTimeout       (-23016)

/* kETIMEDOUTErr: what a MacTCP call reports when it gives up. */
#define net68kTimeoutErr     (kETIMEDOUTErr)

/*
 * OT reports a refused connection as a T_DISCONNECT whose TDiscon.reason
 * carries the real error. When the peer closes without one — or with 0 —
 * the caller still needs a failure code, and connection refused is the
 * only honest one.
 */
#define net68kRefusedErr     (kECONNREFUSEDErr)

/*
 * MacTCPHelper's TCPRoutines.h spells the yield callback this way in C,
 * and the engine above the socket is compiled against that spelling.
 * Keeping it identical is what makes the two transports interchangeable.
 */
typedef void (*GiveTimePtr)(void);

/* One live connection is normal; two lets a reconnect overlap. */
#define kOTMaxStreams 2

#define kOTConnectTimeoutTicks (30UL * 60UL) /* 30 s to reach ESTABLISHED */
#define kOTIdleTimeoutTicks    (60UL * 60UL) /* 60 s without a byte      */
#define kOTCloseTicks          (2UL * 60UL)  /* 2 s to see the peer FIN  */
#define kOTDnsTimeoutTicks     (30UL * 60UL) /* 30 s for a lookup        */

typedef struct OTStream {
    EndpointRef      ep;
    InetAddress      remoteAddr; /* must outlive OTConnect: OT reads it */
    TCall            sndCall;    /* later, when it sends the SYN         */
    volatile Boolean connectComplete;
    volatile Boolean ordRelReceived;
    volatile Boolean disconnectReceived;
    volatile OSStatus lastError;
    Boolean          inUse;
    Boolean          connected;
    Boolean          ordRelConsumed;
    Boolean          asyncOpenPending;
} OTStream;

static OTStream gStreams[kOTMaxStreams];
static int      gCancel = 0;
static Boolean  gOTReady = false;

/* Read by HttpClient for diagnostics; same symbols as the MacTCP build. */
OSErr          gLastMacTCPErr = 0;
OSErr          gLastRecvErr = 0;
unsigned short gLastAmtUnread = 0;

void net_mac_set_cancel(int c) { gCancel = c; }

/* ------------------------------------------------------------------ */
/* Notifier — deferred-task time only: set flags, copy nothing large  */
/* ------------------------------------------------------------------ */
static pascal void ot_notifier(void *context, OTEventCode event,
                               OTResult result, void *cookie)
{
    OTStream *s = (OTStream *)context;
    (void)cookie;

    switch (event) {
    case T_CONNECT:
        s->connectComplete = true;
        /* Consume the event, or every later call fails with kOTLookErr. */
        OTRcvConnect(s->ep, NULL);
        break;

    case T_DATA:
    case T_EXDATA:
        /* Nothing to record: OTRcv is polled and never blocks. */
        break;

    case T_ORDREL:
        s->ordRelReceived = true;
        break;

    case T_DISCONNECT:
        s->disconnectReceived = true;
        if (result < 0) s->lastError = result;
        break;

    default:
        break;
    }
}

/* The reason a peer went away lives in a TDiscon, not in the event's
 * result, and the event has to be consumed either way. */
static void ot_consume_disconnect(OTStream *s)
{
    TDiscon discon;

    if (s->ep == NULL) return;
    OTMemzero(&discon, sizeof(discon));
    discon.udata.maxlen = 0;
    discon.udata.len = 0;
    discon.udata.buf = NULL;
    if (OTRcvDisconnect(s->ep, &discon) == noErr && discon.reason != 0)
        s->lastError = discon.reason;
}

static void ot_pump(GiveTimePtr giveTime)
{
    /* MacYield() in the engine calls SystemTask and yields to any
     * cooperative thread; OT's deferred tasks run from there. */
    if (giveTime) giveTime();
    else SystemTask();
}

static OTStream *ot_stream(unsigned long id)
{
    if (id < 1 || id > (unsigned long)kOTMaxStreams) return NULL;
    return gStreams[id - 1].inUse ? &gStreams[id - 1] : NULL;
}

static OSErr ot_init(void)
{
    OSStatus err;

    if (gOTReady) return noErr;
    err = InitOpenTransport();
    if (err != noErr) {
        gLastMacTCPErr = (OSErr)err;
        return (OSErr)err;
    }
    memset(gStreams, 0, sizeof(gStreams));
    gOTReady = true;
    return noErr;
}

/* MacTCPHelper name, kept because HttpClient.cpp calls it. */
OSErr InitNetwork(void) { return ot_init(); }

/* ------------------------------------------------------------------ */
/* DNS                                                                 */
/* ------------------------------------------------------------------ */
typedef struct {
    InetSvcRef      svc;
    InetHostInfo    hostInfo;
    volatile Boolean complete;
    volatile OSStatus lastError;
    char            name[256]; /* OT reads the name after we return */
} OTResolver;

static OTResolver gResolver;

static pascal void ot_dns_notifier(void *context, OTEventCode event,
                                   OTResult result, void *cookie)
{
    OTResolver *r = (OTResolver *)context;
    (void)cookie;

    if (event == T_DNRSTRINGTOADDRCOMPLETE) {
        r->complete = true;
        if (result != noErr) r->lastError = result;
    }
}

OSErr ConvertStringToAddr(char *name, unsigned long *netNum,
                          void (*giveTime)(void))
{
    OSStatus err;
    unsigned long start;

    if (name == NULL || netNum == NULL) return kOTBadNameErr;
    *netNum = 0;

    err = ot_init();
    if (err != noErr) return (OSErr)err;

    if (gResolver.svc == NULL) {
        gResolver.svc = OTOpenInternetServices(kDefaultInternetServicesPath, 0, &err);
        if (err != noErr || gResolver.svc == NULL) {
            gResolver.svc = NULL;
            return (OSErr)err;
        }
        OTInstallNotifier(gResolver.svc, ot_dns_notifier, &gResolver);
        OTSetAsynchronous(gResolver.svc);
    }

    if (strlen(name) >= sizeof(gResolver.name)) return kOTBadNameErr;
    strcpy(gResolver.name, name);

    gResolver.complete = false;
    gResolver.lastError = 0;
    OTMemzero(&gResolver.hostInfo, sizeof(gResolver.hostInfo));

    err = OTInetStringToAddress(gResolver.svc, gResolver.name, &gResolver.hostInfo);
    if (err != noErr && err != kOTNoDataErr) {
        gLastMacTCPErr = (OSErr)err;
        return (OSErr)err;
    }

    start = TickCount();
    while (!gResolver.complete) {
        if (TickCount() - start > kOTDnsTimeoutTicks) {
            gLastMacTCPErr = net68kTimeoutErr;
            return net68kTimeoutErr;
        }
        ot_pump((GiveTimePtr)giveTime);
    }

    if (gResolver.lastError != noErr) {
        gLastMacTCPErr = (OSErr)gResolver.lastError;
        return (OSErr)gResolver.lastError;
    }

    *netNum = (unsigned long)gResolver.hostInfo.addrs[0];
    return noErr;
}

/* ------------------------------------------------------------------ */
/* Streams                                                             */
/* ------------------------------------------------------------------ */

/* Allocate a TCP endpoint, bound and asynchronous. This is the MacTCP
 * TCPCreate equivalent: an idle, unconnected stream. */
OSErr tcp_create_stream(unsigned long *stream, unsigned long recvLen,
                        GiveTimePtr giveTime, char *cancel)
{
    OSStatus err;
    OSStatus bindErr;
    OTConfigurationRef config;
    OTStream *s = NULL;
    TBind bindReq;
    InetAddress localAddr;
    int i;

    (void)recvLen;   /* OT sizes its own buffers */
    (void)giveTime;
    (void)cancel;

    if (stream == NULL) return (OSErr)kOTBadReferenceErr;
    *stream = 0;

    err = ot_init();
    if (err != noErr) return (OSErr)err;

    for (i = 0; i < kOTMaxStreams; i++) {
        if (!gStreams[i].inUse) { s = &gStreams[i]; break; }
    }
    if (s == NULL) return (OSErr)kOTOutOfMemoryErr;
    memset(s, 0, sizeof(*s));

    config = OTCreateConfiguration("tcp");
    if (config == NULL) return (OSErr)kOTOutOfMemoryErr;

    s->ep = OTOpenEndpoint(config, 0, NULL, &err);
    if (err != noErr || s->ep == NULL) {
        if (s->ep != NULL) OTCloseProvider(s->ep);
        memset(s, 0, sizeof(*s));
        gLastMacTCPErr = (OSErr)err;
        return (OSErr)err;
    }

    /* Bind while the endpoint is still synchronous, so OTBind blocks
     * until it has completed (see the header comment). */
    OTInitInetAddress(&localAddr, 0, kOTAnyInetAddress);
    OTMemzero(&bindReq, sizeof(bindReq));
    bindReq.addr.maxlen = sizeof(localAddr);
    bindReq.addr.len = sizeof(localAddr);
    bindReq.addr.buf = (unsigned char *)&localAddr;
    bindReq.qlen = 0; /* not a listening socket */

    bindErr = OTBind(s->ep, &bindReq, NULL);
    if (bindErr != noErr) {
        OTCloseProvider(s->ep);
        memset(s, 0, sizeof(*s));
        gLastMacTCPErr = (OSErr)bindErr;
        return (OSErr)bindErr;
    }

    err = OTInstallNotifier(s->ep, ot_notifier, s);
    if (err == noErr) err = OTSetAsynchronous(s->ep);
    if (err == noErr) err = OTSetNonBlocking(s->ep);
    if (err != noErr) {
        OTRemoveNotifier(s->ep);
        OTCloseProvider(s->ep);
        memset(s, 0, sizeof(*s));
        gLastMacTCPErr = (OSErr)err;
        return (OSErr)err;
    }

    s->inUse = true;
    *stream = (unsigned long)(i + 1);
    return noErr;
}

static OSStatus ot_set_remote(OTStream *s, unsigned long ipAddress, short port)
{
    OTInitInetAddress(&s->remoteAddr, port, (InetHost)ipAddress);
    OTMemzero(&s->sndCall, sizeof(s->sndCall));
    s->sndCall.addr.maxlen = sizeof(s->remoteAddr);
    s->sndCall.addr.len = sizeof(s->remoteAddr);
    s->sndCall.addr.buf = (unsigned char *)&s->remoteAddr;
    return noErr;
}

/* Blocking connect: the MacTCP OpenConnection equivalent. */
OSErr OpenConnection(unsigned long stream, long remoteHost, short remotePort,
                     Byte timeout, GiveTimePtr giveTime, char *cancel)
{
    OTStream *s = ot_stream(stream);
    OSStatus err;
    unsigned long start;

    if (s == NULL) return (OSErr)kOTBadReferenceErr;
    if (s->connected) return noErr;

    ot_set_remote(s, (unsigned long)remoteHost, remotePort);
    s->connectComplete = false;
    s->disconnectReceived = false;
    s->ordRelReceived = false;
    s->ordRelConsumed = false;
    s->lastError = 0;

    err = OTConnect(s->ep, &s->sndCall, NULL);
    /* kOTNoDataErr means "started, not finished yet" — success here. */
    if (err != noErr && err != kOTNoDataErr) {
        gLastMacTCPErr = (OSErr)err;
        return (OSErr)err;
    }

    start = TickCount();
    for (;;) {
        if (s->connectComplete) break;
        if (s->disconnectReceived) {
            ot_consume_disconnect(s);
            gLastMacTCPErr = (OSErr)(s->lastError ? s->lastError : net68kRefusedErr);
            return gLastMacTCPErr;
        }
        if (cancel != NULL && *cancel) {
            gLastMacTCPErr = -1;
            return -1;
        }
        if (TickCount() - start > (unsigned long)(timeout ? timeout : 30) * 60UL) {
            gLastMacTCPErr = commandTimeout;
            return commandTimeout;
        }
        ot_pump(giveTime);
    }

    s->connected = true;
    return noErr;
}

/* Fire the SYN and return; tcp_wait_connected does the waiting. */
int tcp_open_async(unsigned long stream, unsigned long ipAddress, short port)
{
    OTStream *s = ot_stream(stream);
    OSStatus err;

    if (s == NULL) return -1;

    ot_set_remote(s, ipAddress, port);
    s->connectComplete = false;
    s->disconnectReceived = false;
    s->ordRelReceived = false;
    s->ordRelConsumed = false;
    s->lastError = 0;

    err = OTConnect(s->ep, &s->sndCall, NULL);
    if (err != noErr && err != kOTNoDataErr) {
        gLastMacTCPErr = (OSErr)err;
        s->asyncOpenPending = false;
        return -1;
    }
    s->asyncOpenPending = true;
    return 0;
}

OSErr tcp_wait_connected(GiveTimePtr giveTime, char *cancel)
{
    OTStream *s = NULL;
    unsigned long start;
    int i;

    for (i = 0; i < kOTMaxStreams; i++) {
        if (gStreams[i].inUse && gStreams[i].asyncOpenPending) {
            s = &gStreams[i];
            break;
        }
    }
    if (s == NULL) return -1;

    start = TickCount();
    for (;;) {
        if (s->connectComplete) break;
        if (s->disconnectReceived) {
            ot_consume_disconnect(s);
            s->asyncOpenPending = false;
            gLastMacTCPErr = (OSErr)(s->lastError ? s->lastError : net68kRefusedErr);
            return gLastMacTCPErr;
        }
        if (cancel != NULL && *cancel) {
            s->asyncOpenPending = false;
            gLastMacTCPErr = -1;
            return -1;
        }
        if (TickCount() - start > kOTConnectTimeoutTicks) {
            s->asyncOpenPending = false;
            gLastMacTCPErr = commandTimeout;
            return commandTimeout;
        }
        ot_pump(giveTime);
    }

    s->asyncOpenPending = false;
    s->connected = true;
    gLastMacTCPErr = noErr;
    return noErr;
}

/* MacTCP connectionState codes, which HttpClient tests for 8. */
short tcp_connection_state(unsigned long stream)
{
    OTStream *s = ot_stream(stream);
    OSStatus state;

    if (s == NULL) return 0;
    if (s->disconnectReceived) return 0;
    if (s->ordRelReceived) return 14; /* CLOSE_WAIT */

    state = OTGetEndpointState(s->ep);
    switch (state) {
    case T_DATAXFER:
        return 8; /* ESTABLISHED */
    case T_OUTCON:
        return 6; /* SYN_SENT */
    case T_INCON:
    case T_INREL:
    case T_OUTREL:
        return 14;
    case T_IDLE:
        return 0;
    default:
        return s->connected ? 8 : 0;
    }
}

OSErr SendData(unsigned long stream, Ptr data, unsigned short length,
               Boolean retry, GiveTimePtr giveTime, char *cancel)
{
    OTStream *s = ot_stream(stream);
    unsigned long sent = 0;
    unsigned long start;

    (void)retry; /* OT is always non-blocking here; we loop either way */

    if (s == NULL) return (OSErr)kOTBadReferenceErr;
    if (!s->connected) return (OSErr)kOTOutStateErr;

    start = TickCount();
    while (sent < (unsigned long)length) {
        OTResult r = OTSnd(s->ep, (void *)(data + sent),
                           (size_t)(length - sent), 0);

        if (r > 0) {
            sent += (unsigned long)r;
            start = TickCount();
            continue;
        }
        if (r == kOTFlowErr || r == kOTNoDataErr || r == kOTLookErr) {
            /* kOTLookErr: a notifier event is waiting to be consumed.
             * Yielding lets the deferred task deliver it. */
            if (s->disconnectReceived) {
                ot_consume_disconnect(s);
                return connectionTerminated;
            }
            if (cancel != NULL && *cancel) return -1;
            if (TickCount() - start > kOTIdleTimeoutTicks) return commandTimeout;
            ot_pump(giveTime);
            continue;
        }
        gLastRecvErr = (OSErr)r;
        return (OSErr)r;
    }
    return noErr;
}

OSErr RecvData(unsigned long stream, Ptr data, unsigned short *length,
               Boolean retry, GiveTimePtr giveTime, char *cancel)
{
    OTStream *s = ot_stream(stream);
    unsigned long start;
    unsigned short want;
    OTFlags flags = 0;

    (void)retry;

    if (s == NULL || length == NULL) return (OSErr)kOTBadReferenceErr;

    want = *length;
    *length = 0;
    if (want == 0) return noErr;
    if (!s->connected) {
        /* Half-closed connections still deliver their buffered bytes. */
        if (s->disconnectReceived) return connectionTerminated;
        if (!s->ordRelReceived) return (OSErr)kOTOutStateErr;
    }

    start = TickCount();
    for (;;) {
        OTResult r;

        if (s->disconnectReceived && !s->ordRelReceived) {
            ot_consume_disconnect(s);
            return connectionTerminated;
        }

        r = OTRcv(s->ep, (void *)data, (size_t)want, &flags);

        if (r > 0) {
            *length = (unsigned short)r;
            gLastAmtUnread = (unsigned short)r;
            gLastRecvErr = noErr;
            return noErr;
        }

        if (r == kOTNoDataErr) {
            /* Everything buffered has been delivered. */
            if (s->ordRelReceived) {
                if (!s->ordRelConsumed) {
                    OTRcvOrderlyDisconnect(s->ep);
                    s->ordRelConsumed = true;
                }
                return connectionClosing;
            }
            if (s->disconnectReceived) {
                ot_consume_disconnect(s);
                return connectionTerminated;
            }
            if (cancel != NULL && *cancel) return -1;
            if (TickCount() - start > kOTIdleTimeoutTicks) {
                gLastRecvErr = commandTimeout;
                return -1;
            }
            ot_pump(giveTime);
            continue;
        }

        if (r == kOTLookErr && s->ordRelReceived && !s->ordRelConsumed) {
            OTRcvOrderlyDisconnect(s->ep);
            s->ordRelConsumed = true;
            continue;
        }

        if (r == kOTLookErr && s->disconnectReceived) {
            ot_consume_disconnect(s);
            return connectionTerminated;
        }

        gLastRecvErr = (OSErr)r;
        return (OSErr)r;
    }
}

OSErr CloseConnection(unsigned long stream, GiveTimePtr giveTime, char *cancel)
{
    OTStream *s = ot_stream(stream);
    unsigned long start;

    (void)cancel;

    if (s == NULL || !s->connected) return noErr;

    /* OTSndOrderlyDisconnect sends a FIN: "done sending, still reading". */
    OTSndOrderlyDisconnect(s->ep);
    s->connected = false;

    start = TickCount();
    while (!s->ordRelReceived && !s->disconnectReceived &&
           (TickCount() - start) < kOTCloseTicks) {
        ot_pump(giveTime);
    }
    return noErr;
}

OSErr AbortConnection(unsigned long stream, GiveTimePtr giveTime, char *cancel)
{
    OTStream *s = ot_stream(stream);

    (void)giveTime;
    (void)cancel;

    if (s == NULL) return noErr;
    if (s->connected) OTSndDisconnect(s->ep, NULL);
    s->connected = false;
    return noErr;
}

OSErr ReleaseStream(unsigned long stream, GiveTimePtr giveTime, char *cancel)
{
    OTStream *s = ot_stream(stream);

    (void)giveTime;
    (void)cancel;

    if (s == NULL) return noErr;

    /* Remove the notifier before the struct it points at goes away. */
    if (s->ep != NULL) {
        OTRemoveNotifier(s->ep);
        OTCloseProvider(s->ep);
        s->ep = NULL;
    }
    memset(s, 0, sizeof(*s));
    return noErr;
}

/* ------------------------------------------------------------------ */
/* mbedTLS net_sockets API — the same shape as the MacTCP original     */
/* ------------------------------------------------------------------ */
void mbedtls_net_init(mbedtls_net_context *ctx, void *give_time_ptr)
{
    ctx->fd = -1;
    ctx->give_time_ptr = give_time_ptr;
    ctx->terminated = 0;
}

int mbedtls_net_connect(mbedtls_net_context *ctx, const char *host,
                        const char *port, int proto)
{
    OSErr err;
    unsigned long ipAddress = 0;
    unsigned long stream;
    short portNum = (short)atoi(port);
    GiveTimePtr giveTime = (GiveTimePtr)ctx->give_time_ptr;

    (void)proto;

    err = InitNetwork();
    if (err != noErr) return MBEDTLS_ERR_NET_SOCKET_FAILED;

    err = ConvertStringToAddr((char *)host, &ipAddress, giveTime);
    if (err != noErr) {
        gLastMacTCPErr = err;
        return MBEDTLS_ERR_NET_UNKNOWN_HOST;
    }

    err = tcp_create_stream(&stream, 0, giveTime, NULL);
    if (err != noErr) return MBEDTLS_ERR_NET_SOCKET_FAILED;

    err = OpenConnection(stream, (long)ipAddress, portNum, 30, giveTime, NULL);
    if (err != noErr) {
        ReleaseStream(stream, giveTime, NULL);
        return MBEDTLS_ERR_NET_CONNECT_FAILED;
    }

    ctx->fd = (int)stream;
    return 0;
}

int mbedtls_net_send(void *ctx_v, const unsigned char *buf, size_t len)
{
    mbedtls_net_context *ctx = (mbedtls_net_context *)ctx_v;
    GiveTimePtr giveTime = (GiveTimePtr)ctx->give_time_ptr;
    unsigned long stream = (unsigned long)ctx->fd;
    OSErr err;

    if (ctx->fd < 0) return MBEDTLS_ERR_NET_INVALID_CONTEXT;

    err = SendData(stream, (Ptr)buf, (unsigned short)len, false, giveTime, NULL);
    if (err != noErr) return MBEDTLS_ERR_NET_SEND_FAILED;

    return (int)len;
}

int mbedtls_net_recv(void *ctx_v, unsigned char *buf, size_t len)
{
    mbedtls_net_context *ctx = (mbedtls_net_context *)ctx_v;
    GiveTimePtr giveTime = (GiveTimePtr)ctx->give_time_ptr;
    unsigned long stream = (unsigned long)ctx->fd;
    unsigned short dataLen;
    OSErr err;

    if (ctx->fd < 0) return MBEDTLS_ERR_NET_INVALID_CONTEXT;

    dataLen = (unsigned short)(len > 65535 ? 65535 : len);
    err = RecvData(stream, (Ptr)buf, &dataLen, false, giveTime, NULL);

    if (err == noErr) {
        gLastAmtUnread = dataLen;
        return (int)dataLen;
    }

    if (err == connectionClosing) {
        /* The peer closed and everything it sent has been delivered:
         * 0 tells mbedTLS the connection is closed. */
        ctx->terminated = 1;
        return 0;
    }

    if (err == connectionTerminated || err == -1) {
        ctx->terminated = 1;
        return 0;
    }

    return MBEDTLS_ERR_NET_RECV_FAILED;
}

void mbedtls_net_close(mbedtls_net_context *ctx)
{
    GiveTimePtr giveTime;
    unsigned long stream;

    if (ctx->fd < 0) return;
    giveTime = (GiveTimePtr)ctx->give_time_ptr;
    stream = (unsigned long)ctx->fd;
    CloseConnection(stream, giveTime, NULL);
    ctx->fd = -1;
}

void mbedtls_net_free(mbedtls_net_context *ctx)
{
    GiveTimePtr giveTime;
    unsigned long stream;

    if (ctx->fd < 0) return;
    giveTime = (GiveTimePtr)ctx->give_time_ptr;
    stream = (unsigned long)ctx->fd;

    /* Skip the graceful close when the peer already reset: waiting for a
     * FIN that will never come just burns the close timeout. */
    if (!ctx->terminated) CloseConnection(stream, giveTime, NULL);
    ReleaseStream(stream, giveTime, NULL);

    ctx->fd = -1;
    ctx->terminated = 0;
}
