/* curve25519_sat.c — X25519 scalar multiplication, saturated 8x32 limbs.
 *
 * OPT #8b: replacement for curve25519-donna's 10x25.5-bit signed-limb
 * representation.  Field elements are 8 little-endian uint32 words
 * (same format as uECC's P-256 vli), so the multiply and square reuse
 * the I-cache-resident looped Comba asm already in the build:
 *   uECC_vli_mult   (vli_mult_comba_m68k.s,   64 MULU.L)
 *   uECC_vli_square (vli_square_comba_m68k.s, 36 MULU.L)
 * versus ~100 MULS.L per fmul in the donna representation.
 *
 * Reduction mod p = 2^255-19 uses 2^256 = 38 (mod p): fold the high
 * 8 words of the 16-word product back with a single *38 pass.  Values
 * are kept loosely reduced (< 2^256) everywhere; only fe_tobytes
 * canonicalizes.
 *
 * Ladder is the RFC 7748 Montgomery ladder with conditional POINTER
 * swaps (not constant-time masked swaps — this is a retro hobby client;
 * timing side channels are explicitly out of scope, same as the
 * branchy reduction paths below).
 *
 * The inversion chain is transcribed from donna's crecip (djb's
 * 2^255-21 addition chain), same temporaries, same order.
 *
 * Entry point keeps donna's name and contract:
 *   int curve25519_donna(u8 *mypublic, const u8 *secret, const u8 *bp);
 * so uecc_alt.c needs no changes.
 *
 * Host verification: HTTPS-68K-Speedtest's tests/test_curve25519_sat.c (RFC 7748 vectors,
 * 1000-iteration test, random cross-check vs reference donna).
 */

#include <string.h>
#include <stdint.h>

typedef uint8_t u8;

/* fe: 8 little-endian 32-bit words, value < 2^256, loosely reduced. */
typedef uint32_t fe[8];

#ifdef sat_vli_asm
/* m68k build: looped Comba asm (num_words arg ignored — 8x8 fixed). */
extern void uECC_vli_mult(uint32_t *result, const uint32_t *left,
                          const uint32_t *right, int num_words);
extern void uECC_vli_square(uint32_t *result, const uint32_t *left,
                            int num_words);
#define sat_mul_raw(r, a, b) uECC_vli_mult((r), (a), (b), 8)
#define sat_sqr_raw(r, a)    uECC_vli_square((r), (a), 8)
#else
/* Host fallback: portable schoolbook 8x8 -> 16 words. */
static void sat_mul_raw(uint32_t *r, const uint32_t *a, const uint32_t *b) {
  int i, j;
  memset(r, 0, 16 * sizeof(uint32_t));
  for (i = 0; i < 8; i++) {
    uint64_t carry = 0;
    for (j = 0; j < 8; j++) {
      uint64_t t = (uint64_t)a[i] * b[j] + r[i + j] + carry;
      r[i + j] = (uint32_t)t;
      carry = t >> 32;
    }
    r[i + 8] = (uint32_t)carry;
  }
}
static void sat_sqr_raw(uint32_t *r, const uint32_t *a) {
  sat_mul_raw(r, a, a);
}
#endif

/* r = t mod-ish p: fold high 8 words by 38 (2^256 = 38 mod p), then fold
 * the word-8 carry (<= ~40) once more.  Result < 2^256. */
static void fe_reduce(uint32_t *r, const uint32_t *t) {
  uint64_t acc = 0;
  int i;
  for (i = 0; i < 8; i++) {
    acc += t[i] + (uint64_t)t[i + 8] * 38u;
    r[i] = (uint32_t)acc;
    acc >>= 32;
  }
  acc *= 38u;                       /* carry word ≡ +carry*38 */
  for (i = 0; acc != 0 && i < 8; i++) {
    acc += r[i];
    r[i] = (uint32_t)acc;
    acc >>= 32;
  }
  if (acc) {                        /* wrapped 2^256 once more: +38 */
    acc = 38;
    for (i = 0; acc != 0 && i < 8; i++) {
      acc += r[i];
      r[i] = (uint32_t)acc;
      acc >>= 32;
    }
  }
}

static void fe_mul(uint32_t *r, const uint32_t *a, const uint32_t *b) {
  uint32_t t[16];
  sat_mul_raw(t, a, b);
  fe_reduce(r, t);
}

static void fe_sqr(uint32_t *r, const uint32_t *a) {
  uint32_t t[16];
  sat_sqr_raw(t, a);
  fe_reduce(r, t);
}

/* r = a + b (mod-ish p): on carry out of 2^256, fold +38 (<= twice). */
static void fe_add(uint32_t *r, const uint32_t *a, const uint32_t *b) {
  uint64_t acc = 0;
  int i;
  for (i = 0; i < 8; i++) {
    acc += (uint64_t)a[i] + b[i];
    r[i] = (uint32_t)acc;
    acc >>= 32;
  }
  while (acc) {
    acc = 38;
    for (i = 0; acc != 0 && i < 8; i++) {
      acc += r[i];
      r[i] = (uint32_t)acc;
      acc >>= 32;
    }
  }
}

/* r = a - b (mod-ish p): on borrow, the stored value is a-b+2^256
 * ≡ a-b+38, so subtract 38 (<= twice). */
static void fe_sub(uint32_t *r, const uint32_t *a, const uint32_t *b) {
  int64_t acc = 0;
  int i;
  for (i = 0; i < 8; i++) {
    acc += (int64_t)a[i] - b[i];
    r[i] = (uint32_t)acc;
    acc >>= 32;                     /* arithmetic: 0 or -1 */
  }
  while (acc) {
    acc = -38;
    for (i = 0; acc != 0 && i < 8; i++) {
      acc += r[i];
      r[i] = (uint32_t)acc;
      acc >>= 32;
    }
  }
}

/* r = a * 121665 (mod-ish p): per-word mul + fold carry*38. */
static void fe_mul121665(uint32_t *r, const uint32_t *a) {
  uint64_t acc = 0;
  int i;
  for (i = 0; i < 8; i++) {
    acc += (uint64_t)a[i] * 121665u;
    r[i] = (uint32_t)acc;
    acc >>= 32;
  }
  acc *= 38u;
  for (i = 0; acc != 0 && i < 8; i++) {
    acc += r[i];
    r[i] = (uint32_t)acc;
    acc >>= 32;
  }
  if (acc) {
    acc = 38;
    for (i = 0; acc != 0 && i < 8; i++) {
      acc += r[i];
      r[i] = (uint32_t)acc;
      acc >>= 32;
    }
  }
}

static void fe_frombytes(uint32_t *r, const u8 *s) {
  int i;
  for (i = 0; i < 8; i++)
    r[i] = (uint32_t)s[4 * i] |
           ((uint32_t)s[4 * i + 1] << 8) |
           ((uint32_t)s[4 * i + 2] << 16) |
           ((uint32_t)s[4 * i + 3] << 24);
  r[7] &= 0x7fffffffu;              /* RFC 7748: mask the top bit of u */
}

/* Canonicalize (subtract p up to twice; input < 2^256 <= 2p+37) and
 * serialize little-endian. */
static void fe_tobytes(u8 *s, const uint32_t *a) {
  static const uint32_t P[8] = {
    0xffffffedu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    0xffffffffu, 0xffffffffu, 0xffffffffu, 0x7fffffffu
  };
  uint32_t t[8];
  int pass, i;
  memcpy(t, a, sizeof t);
  for (pass = 0; pass < 2; pass++) {
    uint32_t u[8];
    int64_t acc = 0;
    for (i = 0; i < 8; i++) {
      acc += (int64_t)t[i] - P[i];
      u[i] = (uint32_t)acc;
      acc >>= 32;
    }
    if (acc == 0)                   /* no borrow: t >= p, keep t-p */
      memcpy(t, u, sizeof t);
  }
  for (i = 0; i < 8; i++) {
    s[4 * i]     = (u8)(t[i]);
    s[4 * i + 1] = (u8)(t[i] >> 8);
    s[4 * i + 2] = (u8)(t[i] >> 16);
    s[4 * i + 3] = (u8)(t[i] >> 24);
  }
}

/* One Montgomery ladder step (RFC 7748 formulas):
 *   (x2,z2) <- double, (x3,z3) <- diffadd against x1.
 * 5 fe_mul + 4 fe_sqr + 1 fe_mul121665 + 8 add/sub. */
static void ladderstep(uint32_t *x2, uint32_t *z2,
                       uint32_t *x3, uint32_t *z3, const uint32_t *x1) {
  fe A, B, C, D, AA, BB, E, DA, CB, t;
  fe_add(A, x2, z2);
  fe_sub(B, x2, z2);
  fe_add(C, x3, z3);
  fe_sub(D, x3, z3);
  fe_sqr(AA, A);
  fe_sqr(BB, B);
  fe_mul(DA, D, A);
  fe_mul(CB, C, B);
  fe_add(t, DA, CB);
  fe_sqr(x3, t);                    /* x3 = (DA+CB)^2 */
  fe_sub(t, DA, CB);
  fe_sqr(t, t);
  fe_mul(z3, t, x1);                /* z3 = x1*(DA-CB)^2 */
  fe_mul(x2, AA, BB);               /* x2 = AA*BB */
  fe_sub(E, AA, BB);
  fe_mul121665(t, E);
  fe_add(t, t, AA);
  fe_mul(z2, E, t);                 /* z2 = E*(AA + 121665*E) */
}

/* out = z^(2^255 - 21) = z^-1: djb's addition chain, transcribed from
 * donna's crecip (same temporaries, same order). */
static void fe_invert(uint32_t *out, const uint32_t *z) {
  fe z2, z9, z11, z2_5_0, z2_10_0, z2_20_0, z2_50_0, z2_100_0, t0, t1;
  int i;

  /* 2 */ fe_sqr(z2, z);
  /* 4 */ fe_sqr(t1, z2);
  /* 8 */ fe_sqr(t0, t1);
  /* 9 */ fe_mul(z9, t0, z);
  /* 11 */ fe_mul(z11, z9, z2);
  /* 22 */ fe_sqr(t0, z11);
  /* 2^5 - 2^0 = 31 */ fe_mul(z2_5_0, t0, z9);

  /* 2^6 - 2^1 */ fe_sqr(t0, z2_5_0);
  /* 2^7 - 2^2 */ fe_sqr(t1, t0);
  /* 2^8 - 2^3 */ fe_sqr(t0, t1);
  /* 2^9 - 2^4 */ fe_sqr(t1, t0);
  /* 2^10 - 2^5 */ fe_sqr(t0, t1);
  /* 2^10 - 2^0 */ fe_mul(z2_10_0, t0, z2_5_0);

  /* 2^11 - 2^1 */ fe_sqr(t0, z2_10_0);
  /* 2^12 - 2^2 */ fe_sqr(t1, t0);
  /* 2^20 - 2^10 */ for (i = 2; i < 10; i += 2) { fe_sqr(t0, t1); fe_sqr(t1, t0); }
  /* 2^20 - 2^0 */ fe_mul(z2_20_0, t1, z2_10_0);

  /* 2^21 - 2^1 */ fe_sqr(t0, z2_20_0);
  /* 2^22 - 2^2 */ fe_sqr(t1, t0);
  /* 2^40 - 2^20 */ for (i = 2; i < 20; i += 2) { fe_sqr(t0, t1); fe_sqr(t1, t0); }
  /* 2^40 - 2^0 */ fe_mul(t0, t1, z2_20_0);

  /* 2^41 - 2^1 */ fe_sqr(t1, t0);
  /* 2^42 - 2^2 */ fe_sqr(t0, t1);
  /* 2^50 - 2^10 */ for (i = 2; i < 10; i += 2) { fe_sqr(t1, t0); fe_sqr(t0, t1); }
  /* 2^50 - 2^0 */ fe_mul(z2_50_0, t0, z2_10_0);

  /* 2^51 - 2^1 */ fe_sqr(t0, z2_50_0);
  /* 2^52 - 2^2 */ fe_sqr(t1, t0);
  /* 2^100 - 2^50 */ for (i = 2; i < 50; i += 2) { fe_sqr(t0, t1); fe_sqr(t1, t0); }
  /* 2^100 - 2^0 */ fe_mul(z2_100_0, t1, z2_50_0);

  /* 2^101 - 2^1 */ fe_sqr(t1, z2_100_0);
  /* 2^102 - 2^2 */ fe_sqr(t0, t1);
  /* 2^200 - 2^100 */ for (i = 2; i < 100; i += 2) { fe_sqr(t1, t0); fe_sqr(t0, t1); }
  /* 2^200 - 2^0 */ fe_mul(t1, t0, z2_100_0);

  /* 2^201 - 2^1 */ fe_sqr(t0, t1);
  /* 2^202 - 2^2 */ fe_sqr(t1, t0);
  /* 2^250 - 2^50 */ for (i = 2; i < 50; i += 2) { fe_sqr(t0, t1); fe_sqr(t1, t0); }
  /* 2^250 - 2^0 */ fe_mul(t0, t1, z2_50_0);

  /* 2^251 - 2^1 */ fe_sqr(t1, t0);
  /* 2^252 - 2^2 */ fe_sqr(t0, t1);
  /* 2^253 - 2^3 */ fe_sqr(t1, t0);
  /* 2^254 - 2^4 */ fe_sqr(t0, t1);
  /* 2^255 - 2^5 */ fe_sqr(t1, t0);
  /* 2^255 - 21 */ fe_mul(out, t1, z11);
}

int curve25519_donna(u8 *mypublic, const u8 *secret, const u8 *basepoint) {
  fe x1, a, b, c, d;
  uint32_t *x2 = a, *z2 = b, *x3 = c, *z3 = d, *tp;
  u8 e[32];
  int pos, swap;

  memcpy(e, secret, 32);
  e[0]  &= 248;
  e[31] &= 127;
  e[31] |= 64;

  fe_frombytes(x1, basepoint);

  memset(a, 0, sizeof(fe)); a[0] = 1;   /* x2 = 1 */
  memset(b, 0, sizeof(fe));             /* z2 = 0 */
  memcpy(c, x1, sizeof(fe));            /* x3 = x1 */
  memset(d, 0, sizeof(fe)); d[0] = 1;   /* z3 = 1 */

  swap = 0;
  for (pos = 254; pos >= 0; --pos) {
    int bit = (e[pos >> 3] >> (pos & 7)) & 1;
    swap ^= bit;
    if (swap) {
      tp = x2; x2 = x3; x3 = tp;
      tp = z2; z2 = z3; z3 = tp;
    }
    swap = bit;
    ladderstep(x2, z2, x3, z3, x1);
  }
  if (swap) {
    tp = x2; x2 = x3; x3 = tp;
    tp = z2; z2 = z3; z3 = tp;
  }

  {
    fe zinv;
    fe_invert(zinv, z2);
    fe_mul(x2, x2, zinv);
    fe_tobytes(mypublic, x2);
  }
  return 0;
}
