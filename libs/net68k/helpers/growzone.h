/* growzone.h — emergency memory reserve + grow-zone proc.
   Install once at startup; call GrowZoneIdle() from the event loop so the
   reserve is re-armed (and the low-memory event logged) outside the
   Memory Manager's grow-zone callback, where allocation is unsafe.      */
#pragma once

/* Allocate the 32 KB reserve handle and install the grow-zone proc.
   Call after MaxApplZone()/MoreMasters(). */
void GrowZoneInstall(void);

/* Call once per pass through the main event loop. Logs "low memory" the
   first time the reserve was consumed and tries to re-arm it once the
   heap has room again. Cheap no-op in the normal case.                  */
void GrowZoneIdle(void);
