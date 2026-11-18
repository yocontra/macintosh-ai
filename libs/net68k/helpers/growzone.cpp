/* growzone.cpp — emergency memory reserve + grow-zone proc.
   Classic pattern from Inside Macintosh: Memory — keep a purgeable-on-demand
   reserve handle; when the heap can't satisfy a request, the grow-zone proc
   releases the reserve so the allocation succeeds instead of failing with
   memFullErr (or crashing a caller that doesn't check).                  */

#include <Memory.h>
#include <Events.h>   /* TickCount */
#include "growzone.h"

extern void DebugLog(const char* func, const char* msg);

#define kReserveSize (32L * 1024L)

static Handle        sReserve  = nil;
static GrowZoneUPP   sGZUPP    = nil;
static volatile bool sReleased = false; /* set inside grow-zone proc */
static bool          sLogged   = false; /* "low memory" logged once per release */

/* Grow-zone proc. RULES: may not allocate, move memory, or call anything
   that does (no DebugLog here). Must not release the block the Memory
   Manager is currently working on (GZSaveHnd).                          */
static pascal long GrowZoneHit(Size /*cbNeeded*/)
{
    if (sReserve != nil && *sReserve != nil && sReserve != GZSaveHnd()) {
        EmptyHandle(sReserve); /* frees the 32K block, keeps the handle */
        sReleased = true;
        return kReserveSize;
    }
    return 0; /* nothing more to give */
}

void GrowZoneInstall(void)
{
    sReserve = NewHandle(kReserveSize);
    sGZUPP   = NewGrowZoneUPP(GrowZoneHit);
    SetGrowZone(sGZUPP);
}

void GrowZoneIdle(void)
{
    if (!sReleased) return;

    if (!sLogged) {
        DebugLog("mem", "LOW MEMORY: 32K reserve released");
        sLogged = true;
    }

    /* Try to re-arm the reserve now that we're back in the event loop.
       Rate-limited: each failed ReallocateHandle makes the Memory Manager
       walk/compact/purge the whole heap (re-entering GrowZoneHit) before
       failing — heavy per-event-loop work at 16 MHz exactly when the heap
       is already tight.  Retry every ~5 s instead of every iteration. */
    static long sNextRetryTick = 0;
    if (TickCount() < sNextRetryTick) return;
    sNextRetryTick = TickCount() + 300; /* ~5 s */

    if (sReserve != nil && *sReserve == nil) {
        ReallocateHandle(sReserve, kReserveSize);
        if (MemError() == noErr && *sReserve != nil) {
            DebugLog("mem", "32K reserve re-armed");
            sReleased = false;
            sLogged   = false;
        } else {
            EmptyHandle(sReserve); /* stay released; retry next idle */
        }
    }
}
