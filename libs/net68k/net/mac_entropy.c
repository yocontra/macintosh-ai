/*
 * mac_entropy.c — randomness for the TLS keys, on a Mac with no random-
 * number hardware.
 *
 * The 68k mbedTLS build's own entropy source (psa_stubs.c) is a single
 * TickCount() reading mixed with a stack address. An observer could guess
 * that in a few million tries, and the X25519 key comes from it. This file
 * adds what a 68k Mac does have:
 *
 *   · Interrupt timing. Microseconds() is read back to back. Most gaps
 *     between readings are the same; the long ones are where an interrupt
 *     ran in between (the 60 Hz VBL, ADB, SCSI, the network card, Time
 *     Manager tasks), at points nobody outside the Mac can see. Every gap
 *     is hashed.
 *   · Notes. mac_entropy_note() records when events and network yields
 *     happen, cheaply, and the next poll hashes them.
 *
 * HttpClient also mixes in a seed the previous launch saved, so every
 * launch builds on the ones before it.
 *
 * Accounting is in mac_entropy.h: a poll credits 1 bit per interrupt it
 * saw, and waits for 32 of them. On the test Macs the interrupts come in a
 * fixed rhythm, so their timing earns nothing; what varies is how long
 * each one takes, and that is what the 1 bit is for (see the numbers in
 * mac_entropy.h). Speed Test's "Randomness" section measures it per Mac.
 */

#include <string.h>
#include <Timer.h>
#include <Events.h>
#include <DateTimeUtils.h>
#include "mbedtls/sha256.h"
#include "mbedtls/platform_util.h"
#include "mac_entropy.h"

enum {
    kGapsMax  = 65536,        /* never more gaps a poll...                 */
    kTicksMax = 60,           /* ...or longer than a second                */
    kUsualN   = 31,           /* gaps sampled to find the usual one (odd)  */
    kNotePool = 64            /* power of two                              */
};

static unsigned char s_state[32];        /* running hash of all it has seen */
static unsigned char s_notes[kNotePool];
static unsigned int  s_notePos;
static unsigned long s_noteCount;

static unsigned long now_us(void)
{
    UnsignedWide w;
    Microseconds(&w);
    return w.lo;
}

void mac_entropy_note(unsigned long value)
{
    unsigned long t = now_us() ^ value;
    unsigned int  p = s_notePos;

    s_notes[ p      & (kNotePool - 1)] ^= (unsigned char)t;
    s_notes[(p + 1) & (kNotePool - 1)] ^= (unsigned char)(t >> 8);
    s_notes[(p + 2) & (kNotePool - 1)] ^= (unsigned char)(t >> 16);
    s_notes[(p + 3) & (kNotePool - 1)] ^= (unsigned char)(t >> 24);
    s_notePos = p + 3;        /* overlap one byte, so neighbors mix */
    s_noteCount++;
}

void mac_entropy_take_notes(unsigned char out[32])
{
    mbedtls_sha256_context sha;
    unsigned long extra[3];

    extra[0] = now_us();
    extra[1] = TickCount();
    extra[2] = s_noteCount;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    mbedtls_sha256_update(&sha, s_notes, sizeof(s_notes));
    mbedtls_sha256_update(&sha, (const unsigned char *)extra, sizeof(extra));
    mbedtls_sha256_update(&sha, (const unsigned char *)"notes", 5);
    mbedtls_sha256_finish(&sha, out);
    mbedtls_sha256_free(&sha);
}

/* One timer gap: back-to-back readings, nothing in between. (A first
   version waited a few cycles depending on the last reading, but that only
   added variation anyone could predict, and made the jitter look larger.) */
static unsigned short next_gap(unsigned long *prev)
{
    unsigned long t = now_us();
    unsigned short gap = (unsigned short)(t - *prev);
    *prev = t;
    return gap;
}

void mac_entropy_raw_gaps(unsigned short *gaps, unsigned long n)
{
    unsigned long prev = now_us();
    unsigned long i;

    for (i = 0; i < n; i++)
        gaps[i] = next_gap(&prev);
}

/* An interrupt adds at least half the usual gap. The 4 us keeps the
   timer's own 1-2 us wobble out of it. On the test Macs an interrupt adds
   at least 50 us (SE/30) or 130 us (Classic II), so none is missed. */
int mac_entropy_is_interrupt(unsigned short gap, unsigned short usual)
{
    return gap > usual + usual / 2 + 4;
}

/* The usual gap: the median of a few, so neither an interrupt nor the odd
   0 us reading (seen on the SE/30) can move it. */
static unsigned short usual_gap(unsigned long *prev, mbedtls_sha256_context *sha)
{
    unsigned short g[kUsualN];
    int i, j;

    for (i = 0; i < kUsualN; i++)
        g[i] = next_gap(prev);
    mbedtls_sha256_update(sha, (const unsigned char *)g, sizeof(g));
    for (i = 1; i < kUsualN; i++)
        for (j = i; j > 0 && g[j] < g[j - 1]; j--) {
            unsigned short x = g[j]; g[j] = g[j - 1]; g[j - 1] = x;
        }
    return g[kUsualN / 2];
}

/* Hashes gaps until it has seen MAC_ENTROPY_INTERRUPTS_PER_POLL interrupts,
   or hit a limit. Returns the interrupts seen. */
static unsigned long gather(mbedtls_sha256_context *sha)
{
    unsigned short gaps[64];
    unsigned long  prev  = now_us();
    unsigned long  start = TickCount();
    unsigned long  n = 0, seen = 0;
    unsigned short usual = usual_gap(&prev, sha);
    unsigned int   i = 0;

    while (seen < MAC_ENTROPY_INTERRUPTS_PER_POLL && n < kGapsMax &&
           TickCount() - start < kTicksMax) {
        unsigned short gap = next_gap(&prev);
        gaps[i++] = gap;
        n++;
        if (mac_entropy_is_interrupt(gap, usual))
            seen++;
        if (i == sizeof(gaps) / sizeof(gaps[0])) {
            mbedtls_sha256_update(sha, (const unsigned char *)gaps, sizeof(gaps));
            i = 0;
        }
    }
    if (i)
        mbedtls_sha256_update(sha, (const unsigned char *)gaps, i * sizeof(gaps[0]));
    mbedtls_platform_zeroize(gaps, sizeof(gaps));
    return seen;
}

int mac_entropy_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
    mbedtls_sha256_context sha;
    unsigned char out[32];
    unsigned long extra[4];
    unsigned long seen, credit;
    (void)data;

    mbedtls_sha256_init(&sha);

    /* New state = hash of the old state, the clocks, the notes and the
       gaps, so nothing a poll has seen is ever thrown away. */
    mbedtls_sha256_starts(&sha, 0);
    mbedtls_sha256_update(&sha, s_state, sizeof(s_state));
    extra[0] = TickCount();
    GetDateTime(&extra[1]);
    extra[2] = s_noteCount;
    extra[3] = (unsigned long)&sha;
    mbedtls_sha256_update(&sha, (const unsigned char *)extra, sizeof(extra));
    mbedtls_sha256_update(&sha, s_notes, sizeof(s_notes));
    seen = gather(&sha);
    mbedtls_sha256_finish(&sha, s_state);

    /* What leaves is a hash of the state, never the state itself */
    mbedtls_sha256_starts(&sha, 0);
    mbedtls_sha256_update(&sha, s_state, sizeof(s_state));
    mbedtls_sha256_update(&sha, (const unsigned char *)"out", 3);
    mbedtls_sha256_finish(&sha, out);
    mbedtls_sha256_free(&sha);

    /* mbedTLS counts the bytes returned as entropy: return only the credit.
       Fewer interrupts than expected (a limit hit) means less credit, and
       mbedTLS simply polls again. */
    credit = seen * MAC_ENTROPY_BITS_PER_INTERRUPT / 8;
    if (len > credit)
        len = credit;
    memcpy(output, out, len);
    *olen = len;
    mbedtls_platform_zeroize(out, sizeof(out));
    return 0;
}
