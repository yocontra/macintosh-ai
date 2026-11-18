/*
 * net_sockets_mac.c - MacTCP backend for mbedTLS net_sockets API
 *
 * Implements mbedtls_net_* using MacTCPHelper (TCPHi / CvtAddr).
 * The mbedtls_net_context::fd field stores the MacTCP stream identifier.
 * The mbedtls_net_context::give_time_ptr field stores the GiveTimePtr callback.
 *
 * Receive strategy (OS 7 vs OS 8+ split):
 *   OS 7 / MacTCP:  recv_sys7 polls TCPStatus.amtUnreadData and calls
 *     LowTCPRecvData with a no-op giveTime to avoid re-entrant MacTCP
 *     crashes from cooperative thread switches inside MacTCP's wait loop.
 *   OS 8+ / Open Transport:  recv_standard uses RecvData which works
 *     correctly on Open Transport.
 */

#include <mbedtls/net_sockets.h>
#include <mbedtls/error.h>
#include <string.h>
#include <stdlib.h>

#include <MacTCP.h>
#include <Events.h>
/* Include <Devices.h> for PBControl (used by tcp_connection_state /
 * tcp_open_async).  Guard CFM's CloseConnection first so it doesn't
 * conflict with TCPHi.h's CloseConnection(unsigned long, ...) below. */
#define CloseConnection __cfm_CloseConnection
#include <Devices.h>
#undef CloseConnection
#include <mactcp/TCPHi.h>
#include <mactcp/CvtAddr.h>
/* TCPRoutines.h declares LowTCPNoCopyRcv / LowTCPBfrReturn. */
#include <mactcp/TCPRoutines.h>

#define NET_BUF_SIZE   8192

/* Low-memory global: system version as packed BCD, e.g. 0x0750 = System 7.5 */
#define MAC_SYS_VERSION  (*(short *)0x15A)

/* refNum is a non-static global in TCPRoutines.c, initialised by
 * OpenTCPDriver() (called via InitNetwork).                           */
extern short refNum;

static int _cancel = 0;

/* Set from the app (HttpClient::CancelStream) to abort in-flight transfers.
 * Checked by the MacTCPHelper poll loops (via &_cancel) and by recv_sys7.
 * Cleared by HttpClient at the start of each request.                     */
void net_mac_set_cancel(int c) { _cancel = c; }

/* Last raw MacTCP error — readable from C++ for diagnostics */
OSErr gLastMacTCPErr = 0;

/* ------------------------------------------------------------------ */
/* tcp_create_stream — TCPCreate with a fully-zeroed parameter block.  */
/*                                                                     */
/* MacTCPHelper's CreateStream uses NewBlock() which only initialises  */
/* two fields of TCPiopb, leaving tcpStream and csParam as heap       */
/* garbage.  On real Mac hardware that garbage can match an existing   */
/* stream pointer in MacTCP's table, causing TCPCreate to return       */
/* connectionExists (-23007).  Using a stack-allocated, memset-zeroed  */
/* TCPiopb avoids this.                                                */
/* ------------------------------------------------------------------ */
OSErr tcp_create_stream(unsigned long *stream, unsigned long recvLen,
                        GiveTimePtr giveTime, int *cancel)
{
    /* MacTCP accesses the receive buffer from interrupt level (inside the .IPP
     * driver), so it must live in the system heap — not the application heap.
     * NewPtr() allocates in the app heap; on app exit that memory is freed and
     * the address can be reused by a subsequent launch, causing MacTCP to see
     * a phantom connectionExists (-23007) collision on the next TCPCreate call.
     * NewPtrSys() allocates in the system heap, which persists across launches,
     * so the old address is never recycled for a new TCPCreate buffer.        */
    Ptr recvPtr = NewPtrSys((Size)recvLen);
    if (!recvPtr || MemError() != noErr)
        return MemError() ? MemError() : memFullErr;

    TCPiopb pb;
    memset(&pb, 0, sizeof(pb));
    pb.ioCRefNum  = refNum;
    pb.ioResult   = 1;
    pb.csCode     = TCPCreate;
    pb.csParam.create.rcvBuff    = recvPtr;
    pb.csParam.create.rcvBuffLen = recvLen;
    pb.csParam.create.notifyProc = nil;

    PBControl((ParmBlkPtr)&pb, true);   /* async, poll below */
    while (pb.ioResult > 0) {
        (*giveTime)();
        if (*cancel) { DisposePtr(recvPtr); return -1; }
    }

    if (pb.ioResult != noErr) {
        DisposePtr(recvPtr);
        return (OSErr)pb.ioResult;
    }

    *stream = (unsigned long)pb.tcpStream;
    return noErr;
}

/* Last OSErr seen in mbedtls_net_recv */
OSErr gLastRecvErr = 0;

/* Bytes delivered on most recent mbedtls_net_recv call */
unsigned short gLastAmtUnread = 0;

/* ---------------------------------------------------------------------------
 * recv_standard — OS 8+ / Open Transport receive path.
 *
 * Uses RecvData (TCPRcv) which works correctly on Open Transport.
 * --------------------------------------------------------------------------*/
static int recv_standard(unsigned long stream, unsigned char *buf, size_t len,
                         GiveTimePtr giveTime)
{
    unsigned short dataLen = (unsigned short)(len > 65535 ? 65535 : len);

    OSErr err = RecvData(stream, (Ptr)buf, &dataLen, false, giveTime, &_cancel);
    gLastRecvErr = err;

    if (err == connectionTerminated)
        return -1;  /* signal caller to set ctx->terminated */

    if (err == connectionClosing) {
        gLastAmtUnread = dataLen;
        return (int)dataLen;   /* OT delivers data correctly on connectionClosing */
    }

    if (err != noErr)
        return MBEDTLS_ERR_NET_RECV_FAILED;

    gLastAmtUnread = dataLen;
    return (int)dataLen;
}

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
    unsigned long ipAddress;
    unsigned long stream;
    short portNum = (short)atoi(port);
    GiveTimePtr giveTime = (GiveTimePtr)ctx->give_time_ptr;

    err = InitNetwork();
    if (err != noErr)
        return MBEDTLS_ERR_NET_SOCKET_FAILED;

    err = ConvertStringToAddr((char *)host, &ipAddress, giveTime);
    if (err != noErr) {
        gLastMacTCPErr = err;
        return MBEDTLS_ERR_NET_UNKNOWN_HOST;
    }

    err = tcp_create_stream(&stream, NET_BUF_SIZE, giveTime, &_cancel);
    if (err != noErr)
        return MBEDTLS_ERR_NET_SOCKET_FAILED;

    err = OpenConnection(stream, (long)ipAddress, portNum, 0, giveTime, &_cancel);
    if (err != noErr) {
        ReleaseStream(stream, giveTime, &_cancel);
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

    if (ctx->fd < 0)
        return MBEDTLS_ERR_NET_INVALID_CONTEXT;

    err = SendData(stream, (Ptr)buf, (unsigned short)len, 0, giveTime, &_cancel);
    if (err != noErr)
        return MBEDTLS_ERR_NET_SEND_FAILED;

    return (int)len;
}

/* ---------------------------------------------------------------------------
 * NoOpGiveTime — used as giveTimePtr inside MacTCP calls on System 7.
 *
 * LowTCPRecvData always calls (*giveTimePtr)() in its wait loop; passing nil
 * causes a null-function-pointer crash.  This no-op prevents any cooperative-
 * thread switch from occurring inside MacTCP's driver, eliminating the
 * Thread-Manager reentrancy corruption that crashes System 7 / MacTCP.
 * We pre-confirm data is available via TCPStatus before calling LowTCPRecvData,
 * so the wait loop exits on the first interrupt without needing to yield.
 * --------------------------------------------------------------------------*/
/* NoOpGiveTime — used as giveTimePtr inside LowTCPRecvData.
 *
 * LowTCPRecvData always calls (*giveTimePtr)() in its wait loop; passing
 * nil causes a null-function-pointer crash.  This no-op prevents any
 * cooperative-thread switch from occurring inside MacTCP's driver,
 * eliminating MacTCP reentrancy corruption.  The cooperative thread's
 * MacYield in the outer poll loop already yielded to the main thread,
 * which called WNE to let BasiliskII deliver data; by the time we reach
 * LowTCPRecvData, amtReady bytes are confirmed in MacTCP's buffer and
 * the async TCPRcv completes via interrupt without needing to yield.   */
static void NoOpGiveTime(void) { /* intentional no-op — do not yield here */ }

/* recv_sys7 gives up after this long without a byte.  A connection that
   died while the network was out (the server's close lost with it) stays
   ESTABLISHED in MacTCP and would otherwise be waited on forever, even
   after the network is back.  60 s leaves room for a slow first token. */
#define kRecvIdleTicks (60UL * 60UL)

/* ---------------------------------------------------------------------------
 * recv_sys7 — thread-safe receive for System 7 / MacTCP.
 *
 * MacTCP on System 7 is not re-entrant with the Thread Manager: if giveTime()
 * triggers a cooperative-thread switch while MacTCP's internal polling loop
 * is running, MacTCP global state gets corrupted and crashes.
 *
 * Fix: poll TCPStatus.amtUnreadData ourselves (yielding via giveTime between
 * polls), then call LowTCPRecvData with NoOpGiveTime so MacTCP's own wait
 * loop never causes a thread switch — eliminating the unsafe context switch.
 * --------------------------------------------------------------------------*/
static int recv_sys7(unsigned long stream, unsigned char *buf, size_t len,
                     GiveTimePtr giveTime)
{
    unsigned short dataLen = (unsigned short)(len > 65535 ? 65535 : len);
    unsigned short amtReady = 0;  /* bytes confirmed buffered by TCPStatus */

    /* Poll until data arrives or the connection closes. */
    static TCPiopb pb; // §7 MEMORY: static keeps 170-byte struct off the stack per iteration
    unsigned long waitStart = TickCount();
    for (;;) {
        if (_cancel)
            return -1;  /* canceled by app — caller sets ctx->terminated */
        if (TickCount() - waitStart > kRecvIdleTicks) {
            /* Dead connection: terminated, so it's released without the
               graceful close that would wait out MacTCP's own timeout */
            gLastRecvErr = commandTimeout;
            return -1;
        }

        memset(&pb, 0, sizeof(pb)); // §7 MEMORY: zero before each use
        pb.ioCRefNum = refNum;
        pb.ioResult  = 1;
        pb.tcpStream = (StreamPtr)stream;
        pb.csCode    = TCPStatus;
        PBControl((ParmBlkPtr)&pb, false);

        if (pb.ioResult != noErr)
            break;  /* can't get status — attempt read below */

        short state = (short)pb.csParam.status.connectionState;
        if (state == 0)                                          /* CLOSED     */
            return 0;
        if (state == 14 && pb.csParam.status.amtUnreadData == 0) /* CLOSE_WAIT */
            return 0;
        if (pb.csParam.status.amtUnreadData > 0) {
            amtReady = pb.csParam.status.amtUnreadData;
            break;  /* data ready — read without yielding inside MacTCP */
        }

        if (giveTime) giveTime();  /* yield between polls — safe here */
        /* Release BasiliskII host CPU so its network thread can deliver
         * incoming TCP data.  WNE(sleep=1) calls idle_wait/pthread_cond_wait,
         * yielding the host until a tick fires.  Called between TCPStatus
         * polls (never inside a MacTCP call) so there is no reentrancy.  */
        /* Yield CPU so BasiliskII's network thread can deliver TCP data.
         * Exclude updateEvt (0x40) and activateEvt (0x100): their handlers
         * run from deep inside the ssl_read→recv callback call chain and
         * can overflow the stack.  Other event types (null, mouse, key)
         * are returned but rarely dispatched in this context.           */
        { EventRecord _e; WaitNextEvent(everyEvent & ~(updateMask | activMask), &_e, 1, nil); }
    }

    /* Cap the read to bytes already in MacTCP's buffer.
     * LowTCPRecvData spins with NoOpGiveTime; requesting more than amtReady
     * bytes would block waiting for network data that BasiliskII can't deliver
     * without a WaitNextEvent — causing a hang/crash.                       */
    if (amtReady > 0 && amtReady < dataLen)
        dataLen = amtReady;

    /* Call LowTCPRecvData with NoOpGiveTime: MacTCP's wait loop calls it but
     * it does nothing — no thread switch, no MacTCP reentrancy.
     * dataLen is capped to amtReady so the async TCPRcv only requests bytes
     * already confirmed in MacTCP's buffer and completes via interrupt.   */
    Boolean urgent = false, mark = false;
    OSErr err = LowTCPRecvData((StreamPtr)stream, 30, &urgent, &mark,
                               (Ptr)buf, &dataLen,
                               false, nil, NoOpGiveTime, &_cancel);
    gLastRecvErr = err;

    if (err == connectionTerminated)
        return -1;
    if (err == connectionClosing) {
        gLastAmtUnread = dataLen;
        return (int)dataLen;
    }
    if (err != noErr)
        return MBEDTLS_ERR_NET_RECV_FAILED;

    gLastAmtUnread = dataLen;
    return (int)dataLen;
}

int mbedtls_net_recv(void *ctx_v, unsigned char *buf, size_t len)
{
    mbedtls_net_context *ctx = (mbedtls_net_context *)ctx_v;
    GiveTimePtr giveTime = (GiveTimePtr)ctx->give_time_ptr;
    unsigned long stream = (unsigned long)ctx->fd;

    if (ctx->fd < 0)
        return MBEDTLS_ERR_NET_INVALID_CONTEXT;

    /* System 7 / MacTCP: use thread-safe polling path to avoid crashing
     * MacTCP's non-re-entrant wait loop via cooperative thread switches.
     * System 8+ / Open Transport: RecvData handles all states correctly. */
    int n;
    if (MAC_SYS_VERSION < 0x0800)
        n = recv_sys7(stream, buf, len, giveTime);
    else
        n = recv_standard(stream, buf, len, giveTime);

    if (n == -1) {
        ctx->terminated = 1;
        return 0;
    }
    return n;
}

void mbedtls_net_close(mbedtls_net_context *ctx)
{
    if (ctx->fd < 0)
        return;
    GiveTimePtr giveTime = (GiveTimePtr)ctx->give_time_ptr;
    unsigned long stream = (unsigned long)ctx->fd;
    CloseConnection(stream, giveTime, &_cancel);
    ctx->fd = -1;
}

void mbedtls_net_free(mbedtls_net_context *ctx)
{
    if (ctx->fd < 0)
        return;
    GiveTimePtr giveTime = (GiveTimePtr)ctx->give_time_ptr;
    unsigned long stream = (unsigned long)ctx->fd;
    /* Skip graceful close when peer already sent RST — CloseConnection()
     * can block for MacTCP's full close-wait timeout on a dead stream. */
    if (!ctx->terminated)
        CloseConnection(stream, giveTime, &_cancel);
    ReleaseStream(stream, giveTime, &_cancel);
    ctx->fd = -1;
    ctx->terminated = 0;
}

/* ------------------------------------------------------------------ */
/* OPT-1: TCP connection health check                                 */
/* Returns the connectionState byte from TCPStatus:                   */
/*   0 = CLOSED, 6 = SYN_SENT, 8 = ESTABLISHED, 14 = CLOSE_WAIT     */
/* ------------------------------------------------------------------ */
short tcp_connection_state(unsigned long stream)
{
    TCPiopb pb;
    memset(&pb, 0, sizeof(pb));
    pb.ioCRefNum  = refNum;
    pb.ioResult   = 1;
    pb.tcpStream  = (StreamPtr)stream;
    pb.csCode     = TCPStatus;
    PBControl((ParmBlkPtr)&pb, false); /* synchronous: fast status query */
    if (pb.ioResult != noErr)
        return 0;
    return (short)pb.csParam.status.connectionState;
}

/* ------------------------------------------------------------------ */
/* OPT-6: Async TCPActiveOpen — fires the SYN without blocking.       */
/* The static pBlock is valid until tcp_wait_connected() consumes it. */
/* ------------------------------------------------------------------ */
static TCPiopb g_async_open_pb;
static int     g_async_open_pending = 0;

int tcp_open_async(unsigned long stream, unsigned long ipAddress, short port)
{
    memset(&g_async_open_pb, 0, sizeof(g_async_open_pb));
    g_async_open_pb.ioCRefNum  = refNum;
    g_async_open_pb.ioCompletion = nil;
    g_async_open_pb.ioResult   = 1;
    g_async_open_pb.tcpStream  = (StreamPtr)stream;
    g_async_open_pb.csCode     = TCPActiveOpen;
    g_async_open_pb.csParam.open.ulpTimeoutValue    = 30;
    g_async_open_pb.csParam.open.ulpTimeoutAction   = 1;
    g_async_open_pb.csParam.open.validityFlags      = 0xC0;
    g_async_open_pb.csParam.open.commandTimeoutValue = 30;
    g_async_open_pb.csParam.open.remoteHost = (ip_addr)ipAddress;
    g_async_open_pb.csParam.open.remotePort = (tcp_port)port;
    g_async_open_pb.csParam.open.localPort  = 0;

    PBControl((ParmBlkPtr)&g_async_open_pb, true); /* async: returns immediately */
    g_async_open_pending = 1;
    return 0;
}

/* OPT-6: Poll until TCP is ESTABLISHED (or error / cancel).          */
OSErr tcp_wait_connected(GiveTimePtr giveTime, bool *cancel)
{
    if (!g_async_open_pending)
        return -1;

    while (g_async_open_pb.ioResult > 0) {
        giveTime();
        if (*cancel) {
            g_async_open_pending = 0;
            gLastMacTCPErr = -1;
            return -1;
        }
    }

    g_async_open_pending = 0;
    OSErr err = g_async_open_pb.ioResult;
    gLastMacTCPErr = err;
    return err;
}
