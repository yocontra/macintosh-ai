/* mac_entropy.h — an entropy source for mbedTLS on a 68k Mac.
   See mac_entropy.c for what it gathers and why. */
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Accounting. The gaps between Microseconds() readings are only
   unpredictable where an interrupt ran in between, so credit goes by
   interrupts, not by gaps: each one counts MAC_ENTROPY_BITS_PER_INTERRUPT.
   A poll waits for MAC_ENTROPY_INTERRUPTS_PER_POLL of them (32 bits), and
   a gather stops at MAC_ENTROPY_THRESHOLD credited bytes (64 bits).
   Seeding the generator gathers twice (128 bits), a reseed once.

   Measured with Speed Test (October 2026): both test Macs see about
   130-160 interrupts a second. When they come is mostly a fixed rhythm,
   but how long each takes varies: at 4 us resolution, at least 1.3 bits
   on the SE/30 and 1.8 on the Classic II (most-common-value estimate,
   99% bound). 1 bit leaves room for the timer's own 2 us wobble. */
#define MAC_ENTROPY_BITS_PER_INTERRUPT   1
#define MAC_ENTROPY_INTERRUPTS_PER_POLL  32
#define MAC_ENTROPY_THRESHOLD            8

/* The source itself, for mbedtls_entropy_add_source(..., STRONG). */
int mac_entropy_poll(void *data, unsigned char *output, size_t len, size_t *olen);

/* Records when something happened (an event, a network yield), cheaply.
   The next poll hashes what was recorded. Call it as often as you like. */
void mac_entropy_note(unsigned long value);

/* A hash of the notes so far and the time, for mixing into the generator
   between seedings (mbedtls_ctr_drbg_update). Cheap, and credited nothing. */
void mac_entropy_take_notes(unsigned char out[32]);

/* For Speed Test: the gaps the poll hashes, raw, so the jitter on a real
   Mac can be measured. Fills gaps[0..n-1]. */
void mac_entropy_raw_gaps(unsigned short *gaps, unsigned long n);

/* For Speed Test: is this gap long enough to count as an interrupt, given
   the usual gap? The same test the poll uses. */
int mac_entropy_is_interrupt(unsigned short gap, unsigned short usual);

#ifdef __cplusplus
}
#endif
