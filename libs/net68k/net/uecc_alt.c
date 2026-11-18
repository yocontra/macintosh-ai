/*
 * uecc_alt.c - micro-ecc backed P-256/P-384 alternative implementations for mbedTLS.
 *
 * Replaces:
 *   mbedtls_ecdh_gen_public       (MBEDTLS_ECDH_GEN_PUBLIC_ALT)
 *   mbedtls_ecdh_compute_shared   (MBEDTLS_ECDH_COMPUTE_SHARED_ALT)
 *   mbedtls_ecdsa_verify          (MBEDTLS_ECDSA_VERIFY_ALT)
 *
 * ECDH uses P-256 only (TLS ECDHE-ECDSA cipher suite).
 * ECDSA verify handles P-256 and P-384 via micro-ecc.
 * P-384 is needed to verify intermediate certs signed by P-384 roots
 * (e.g. Google WE1 intermediate signed by GTS Root R4 / secp384r1).
 * Other curves fall back to mbedTLS's own ecp_muladd.
 */

#include <string.h>
#include "mbedtls/ecdh.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/bignum.h"
#include "mbedtls/error.h"

/* Configure micro-ecc: P-256 only, 32-bit words, max speed */
#define uECC_SUPPORTS_secp160r1  0
#define uECC_SUPPORTS_secp192r1  0
#define uECC_SUPPORTS_secp224r1  0
#define uECC_SUPPORTS_secp256r1  1
#define uECC_SUPPORTS_secp256k1  0
#define uECC_OPTIMIZATION_LEVEL  2
#define uECC_SQUARE_FUNC         1
#define uECC_WORD_SIZE           4
#define uECC_VLI_NATIVE_LITTLE_ENDIAN 0

#include "uECC.h"

/* OPT-3: C-callable persistent ECDSA cache functions (implemented in prefs_io.cpp) */
extern void LoadPersistECDSASlot_c(int slot,
                                    unsigned char q_pub_out[64],
                                    unsigned char hash_out[32],
                                    unsigned char *hash_len_out,
                                    unsigned char *valid_out);
extern void SavePersistECDSASlot_c(int slot,
                                    const unsigned char q_pub_in[64],
                                    const unsigned char hash_in[32],
                                    unsigned char hash_len_in);
extern void SaveAllPersistECDSASlots_c(const unsigned char *q_pub_flat,
                                        const unsigned char *hash_flat,
                                        const unsigned char *hash_lens,
                                        const unsigned char *valids,
                                        int num_slots);
extern void LoadAllPersistECDSASlots_c(unsigned char *q_pub_flat,
                                        unsigned char *hash_flat,
                                        unsigned char *hash_lens,
                                        unsigned char *valids,
                                        int num_slots);

/* OPT-3: tracks whether any persistent cache slot was updated this session */
static int g_persist_ecdsa_dirty = 0;
/* OPT-3: tracks whether persistent cache has been loaded into in-memory cache */
static int g_persist_ecdsa_loaded = 0;

#define P256_BYTES 32
#define MAX_ECC_BYTES 32   /* P-256 only */

/* Last ECDH group ID actually used — readable from C++ for diagnostics */
int g_last_ecdh_grp_id = 0;

/* Hooks into fast_alloc.c — activate pool allocator around mbedtls_ecp_mul */
extern void fast_alloc_begin(void);
extern void fast_alloc_end(void);

/* curve25519-donna: fast X25519 scalar mult (all args little-endian 32 bytes) */
extern int curve25519_donna(uint8_t *mypublic, const uint8_t *secret, const uint8_t *basepoint);

/* 2-bit windowed Shamir verify — implemented in uECC.c */
extern void uecc_shamir2_precompute(const uint32_t *Q_native, uint32_t *table, uECC_Curve curve);
extern void uecc_shamir2_precompute_pair(const uint32_t *P_native, const uint32_t *Q_native,
                                          uint32_t *table, uECC_Curve curve);
extern int  uecc_verify_precomp(const uint8_t *pub, const uint8_t *hash, unsigned hlen,
                                 const uint8_t *sig, const uint32_t *table, uECC_Curve curve);
extern int  uecc_verify_precomp4(const uint8_t *hash, unsigned hlen, const uint8_t *sig,
                                  const uint32_t *tableA, const uint32_t *tableB, uECC_Curve curve);
extern void uecc_point_double_n_p256(uint32_t *px, uint32_t *py, int n, uECC_Curve curve);
extern void uecc_p256_bytes_to_native(uint32_t *native, const uint8_t *bytes32);

/* EC midpoint persistence — implemented in prefs_io.cpp */
extern void LoadECMidpoint_c(unsigned char q_pub_out[64],
                              unsigned char q2_native_out[64],
                              unsigned char *valid_out);
extern void SaveECMidpoint_c(const unsigned char q_pub_in[64],
                              const unsigned char q2_native_in[64]);

/* Debug timing — C-callable bridge defined in debug_con.cpp */
extern void uecc_con_log(const char *tag, const char *msg);
#define ConLog uecc_con_log

/* ------------------------------------------------------------------ */
/* RNG bridge: convert mbedTLS f_rng to micro-ecc RNG signature        */
/* ------------------------------------------------------------------ */
static int (*g_f_rng)(void *, unsigned char *, size_t) = NULL;
static void *g_p_rng = NULL;

static int uecc_rng_bridge(uint8_t *dest, unsigned size)
{
    if (g_f_rng == NULL) return 0;
    return (g_f_rng(g_p_rng, dest, (size_t)size) == 0) ? 1 : 0;
}

static void set_rng(int (*f_rng)(void *, unsigned char *, size_t), void *p_rng)
{
    g_f_rng = f_rng;
    g_p_rng = p_rng;
    uECC_set_rng(uecc_rng_bridge);
}

/* ------------------------------------------------------------------ */
/* Helpers: convert between mbedTLS MPI and big-endian byte arrays     */
/* ------------------------------------------------------------------ */
static int mpi_to_be32(const mbedtls_mpi *m, uint8_t *buf)
{
    return mbedtls_mpi_write_binary(m, buf, P256_BYTES);
}

static int be32_to_mpi(mbedtls_mpi *m, const uint8_t *buf)
{
    return mbedtls_mpi_read_binary(m, buf, P256_BYTES);
}

/* Cached ECDSA P-256 verify results — 2 slots.
 * TLS 1.2 ECDHE_ECDSA calls mbedtls_ecdsa_verify twice per handshake:
 *   slot 0: cert chain verify  (Q = WE1 intermediate pub, hash = server cert hash — CONSTANT)
 *   slot 1: ServerKeyExchange  (Q = server cert pub, hash includes nonces  — changes each connection)
 * Two slots prevent the SKE verify from evicting the cert verify hit.
 * Each key keeps its own slot; a new key takes an empty slot, or evicts
 * round-robin (g_ecdsa_cache_next) once both are full. */
#define ECDSA_CACHE_SLOTS 2
static uint8_t g_ecdsa_cache_pub [ECDSA_CACHE_SLOTS][P256_BYTES * 2];
static uint8_t g_ecdsa_cache_hash[ECDSA_CACHE_SLOTS][64];
static size_t  g_ecdsa_cache_blen[ECDSA_CACHE_SLOTS];
static int     g_ecdsa_cache_valid[ECDSA_CACHE_SLOTS];
static int     g_ecdsa_cache_next = 0;   /* next slot to evict */

/* Pre-computed X25519 ephemeral key (d, d*G).
 * Computed before TCP connect so gen_public is instant during the handshake,
 * leaving only compute_shared (~9.5s) inside the server's timeout window. */
static uint8_t g_x25519_pre_priv[P256_BYTES];
static uint8_t g_x25519_pre_pub[P256_BYTES];   /* u-coordinate only */
static int     g_x25519_pre_ready = 0;

int uecc_x25519_pre_ready(void) { return g_x25519_pre_ready; }

/* Precomputed 2-bit windowed Shamir table for fast SKE verify.
 * Built from g_ecdsa_cache_pub[1] (server cert pub key = slot 1 = SKE verify key).
 * P256: 18 entries (pp+pm, 3×3 each) × 8 words X + 8 words Y = 288 uint32_t = 1152 bytes.
 * g_shamir2_Q: the pub key bytes (64 bytes) for which the table was built. */
#define SHAMIR2_NWORDS 8          /* P256 num_words = 8 */
#define SHAMIR2_TABLE_WORDS (18 * SHAMIR2_NWORDS * 2)  /* 288 words */
static uint32_t g_shamir2_table[SHAMIR2_TABLE_WORDS];   /* pair (G,  Q)  */
static uint32_t g_shamir2_table2[SHAMIR2_TABLE_WORDS];  /* pair (G2, Q2) — OPT #7 */
static uint8_t  g_shamir2_Q[P256_BYTES * 2];  /* X+Y, 64 bytes */
static int      g_shamir2_ready = 0;          /* both tables built */

/* G2 = 2^128 * G on P-256, uECC native little-endian word format (X then Y).
 * Universal constant — computed offline and triple-checked (on-curve,
 * n*G2 = infinity, 2*(2^127*G) cross-check). Enables splitting u1,u2 at
 * bit 128 so the verify loop runs ~129 doublings instead of ~257. */
static const uint32_t kG2_native[SHAMIR2_NWORDS * 2] = {
    /* X */
    0xD789BD85, 0x57C84FC9, 0xC297EAC3, 0xFC35FF7D,
    0x88C6766E, 0xFB982FD5, 0xEEDB5E67, 0x447D739B,
    /* Y */
    0x72E25B32, 0x0C7E33C9, 0xA7FAE500, 0x3D349B95,
    0x3A4AAFF7, 0xE12E9D95, 0x834131EE, 0x2D4825AB
};

/* Build Shamir2 table from server cert pub key (ECDSA cache slot 1).
 * Called after uecc_load_persist_ecdsa_cache() and after SslClose saves new data.
 * Takes ~1.1s on SE/30 (12 affine ops) — runs in warm-up, off handshake critical path. */
void uecc_precompute_shamir2_table(void)
{
    uint32_t Q_native[SHAMIR2_NWORDS * 2];  /* X (8 words) + Y (8 words) */

    /* Find the SKE-verify slot (Q = server cert pub). Normally slot 1,
     * but if the cert-chain verify never populated slot 0, the SKE verify
     * lands in slot 0 instead. Scan from slot 1 down and take the first
     * valid slot. */
    int slot = -1, i;
    for (i = ECDSA_CACHE_SLOTS - 1; i >= 0; i--) {
        if (g_ecdsa_cache_valid[i]) { slot = i; break; }
    }
    if (slot < 0) {                          /* no slot populated yet */
        ConLog("shamir2", "skip-no-slot");   /* visible: fast verify path off */
        return;
    }
    /* Convert big-endian pub bytes to uECC native word format */
    uecc_p256_bytes_to_native(Q_native,                  g_ecdsa_cache_pub[slot]);
    uecc_p256_bytes_to_native(Q_native + SHAMIR2_NWORDS, g_ecdsa_cache_pub[slot] + P256_BYTES);
    uecc_shamir2_precompute(Q_native, g_shamir2_table, uECC_secp256r1());

    /* OPT #7: second table over (G2, Q2) for the 4-scalar split verify.
     * Q2 = 2^128*Q costs 128 Jacobian doublings (~0.5s on SE/30), so it is
     * persisted per cert Q and only recomputed when the cert rotates. */
    {
        uint32_t Q2_native[SHAMIR2_NWORDS * 2];
        unsigned char mid_q[P256_BYTES * 2];
        unsigned char mid_q2[P256_BYTES * 2];
        unsigned char mid_valid = 0;

        LoadECMidpoint_c(mid_q, mid_q2, &mid_valid);
        if (mid_valid &&
            memcmp(mid_q, g_ecdsa_cache_pub[slot], P256_BYTES * 2) == 0) {
            memcpy(Q2_native, mid_q2, sizeof(Q2_native));
            ConLog("shamir2", "mid-hit");
        } else {
            memcpy(Q2_native, Q_native, sizeof(Q2_native));
            uecc_point_double_n_p256(Q2_native, Q2_native + SHAMIR2_NWORDS,
                                     128, uECC_secp256r1());
            SaveECMidpoint_c(g_ecdsa_cache_pub[slot],
                             (const unsigned char *)Q2_native);
            ConLog("shamir2", "mid-calc");
        }
        uecc_shamir2_precompute_pair(kG2_native, Q2_native,
                                     g_shamir2_table2, uECC_secp256r1());
    }

    memcpy(g_shamir2_Q, g_ecdsa_cache_pub[slot], P256_BYTES * 2);
    g_shamir2_ready = 1;
    ConLog("shamir2", "ready");
}

/* OPT-3: load persistent ECDSA cache from prefs into the in-memory slots.
 * Single file read via LoadAllPersistECDSASlots_c (avoids per-slot re-read). */
void uecc_load_persist_ecdsa_cache(void)
{
    unsigned char hash_lens[ECDSA_CACHE_SLOTS];
    unsigned char valids[ECDSA_CACHE_SLOTS];
    int slot;
    LoadAllPersistECDSASlots_c(
        (unsigned char *)g_ecdsa_cache_pub,
        (unsigned char *)g_ecdsa_cache_hash,
        hash_lens, valids, ECDSA_CACHE_SLOTS);
    for (slot = 0; slot < ECDSA_CACHE_SLOTS; slot++) {
        g_ecdsa_cache_blen[slot]  = (size_t)hash_lens[slot];
        g_ecdsa_cache_valid[slot] = valids[slot];
    }
    g_persist_ecdsa_loaded = 1;
    g_persist_ecdsa_dirty  = 0;
}

/* OPT-3: flush in-memory cache back to prefs file in a single write */
void uecc_save_persist_ecdsa_cache(void)
{
    unsigned char hash_lens[ECDSA_CACHE_SLOTS];
    unsigned char valids[ECDSA_CACHE_SLOTS];
    int slot;
    for (slot = 0; slot < ECDSA_CACHE_SLOTS; slot++) {
        hash_lens[slot] = (unsigned char)g_ecdsa_cache_blen[slot];
        valids[slot]    = (unsigned char)g_ecdsa_cache_valid[slot];
    }
    SaveAllPersistECDSASlots_c(
        (const unsigned char *)g_ecdsa_cache_pub,
        (const unsigned char *)g_ecdsa_cache_hash,
        hash_lens, valids, ECDSA_CACHE_SLOTS);
    g_persist_ecdsa_dirty = 0;
}

/* OPT-3: returns 1 if in-memory cache was updated and needs flushing */
int uecc_persist_ecdsa_dirty(void) { return g_persist_ecdsa_dirty; }

/* OPT-5: pre-size MPI limb arrays to avoid realloc during scalar mult */
static void PreGrowMpi(mbedtls_mpi *X)
{
    mbedtls_mpi_grow(X, 8); /* 8 x 32-bit limbs = 256 bits */
}

/* §dedup DEDUP: collapse three consecutive PreGrowMpi calls for X/Y/Z into one */
static void PreGrowPoint(mbedtls_ecp_point *P)
{
    PreGrowMpi(&P->MBEDTLS_PRIVATE(X));
    PreGrowMpi(&P->MBEDTLS_PRIVATE(Y));
    PreGrowMpi(&P->MBEDTLS_PRIVATE(Z));
}

int uecc_precompute_x25519_key(int (*f_rng)(void *, unsigned char *, size_t),
                                void *p_rng)
{
    static const uint8_t basepoint[P256_BYTES] = { 9 };
    uint8_t priv_le[P256_BYTES], pub_le[P256_BYTES];
    int i, ret;

    ret = f_rng(p_rng, priv_le, P256_BYTES);
    if (ret) return ret;

    /* X25519 scalar clamping (RFC 7748 §5) */
    priv_le[0]  &= 248;
    priv_le[31] &= 127;
    priv_le[31] |= 64;

    /* pub = priv * G — donna is ~4× faster than mbedTLS generic ecp_mul */
    curve25519_donna(pub_le, priv_le, basepoint);

    /* Reverse LE → BE so be32_to_mpi / mbedtls_mpi_write_binary work correctly */
    for (i = 0; i < P256_BYTES; i++) {
        g_x25519_pre_priv[i] = priv_le[P256_BYTES - 1 - i];
        g_x25519_pre_pub[i]  = pub_le[P256_BYTES - 1 - i];
    }
    g_x25519_pre_ready = 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* ECDH: generate ephemeral key pair                                   */
/* ------------------------------------------------------------------ */
int mbedtls_ecdh_gen_public(mbedtls_ecp_group *grp, mbedtls_mpi *d,
                             mbedtls_ecp_point *Q,
                             int (*f_rng)(void *, unsigned char *, size_t),
                             void *p_rng)
{
    int ret;

    if (grp->id != MBEDTLS_ECP_DP_SECP256R1) {
        if (grp->id == MBEDTLS_ECP_DP_CURVE25519 && g_x25519_pre_ready) {
            /* Use pre-computed X25519 key — gen_public is now instant */
            ret = be32_to_mpi(d, g_x25519_pre_priv);
            if (ret == 0) ret = be32_to_mpi(&Q->MBEDTLS_PRIVATE(X), g_x25519_pre_pub);
            if (ret == 0) ret = mbedtls_mpi_lset(&Q->MBEDTLS_PRIVATE(Z), 1);
            memset(g_x25519_pre_priv, 0, P256_BYTES);
            g_x25519_pre_ready = 0;
            return ret;
        }
        /* Fallback: compute d*G with pool allocator */
        ret = mbedtls_ecp_gen_privkey(grp, d, f_rng, p_rng);
        if (ret != 0) return ret;
        fast_alloc_begin();
        ret = mbedtls_ecp_mul(grp, Q, d, &grp->G, f_rng, p_rng);
        fast_alloc_end();
        return ret;
    }

    uint8_t priv[P256_BYTES];
    uint8_t pub[P256_BYTES * 2];

    set_rng(f_rng, p_rng);
    if (!uECC_make_key(pub, priv, uECC_secp256r1())) {
        return MBEDTLS_ERR_ECP_RANDOM_FAILED;
    }

    ret = be32_to_mpi(d, priv);
    if (ret == 0) ret = be32_to_mpi(&Q->MBEDTLS_PRIVATE(X), pub);
    if (ret == 0) ret = be32_to_mpi(&Q->MBEDTLS_PRIVATE(Y), pub + P256_BYTES);
    if (ret == 0) ret = mbedtls_mpi_lset(&Q->MBEDTLS_PRIVATE(Z), 1);

    memset(priv, 0, sizeof(priv));
    return ret;
}

/* ------------------------------------------------------------------ */
/* ECDH: compute shared secret                                         */
/* ------------------------------------------------------------------ */
int mbedtls_ecdh_compute_shared(mbedtls_ecp_group *grp, mbedtls_mpi *z,
                                 const mbedtls_ecp_point *Q,
                                 const mbedtls_mpi *d,
                                 int (*f_rng)(void *, unsigned char *, size_t),
                                 void *p_rng)
{
    int ret;

    g_last_ecdh_grp_id = (int)grp->id;

    if (grp->id != MBEDTLS_ECP_DP_SECP256R1) {
        /* X25519 path — use curve25519-donna.
         * NOTE: No shared-secret cache here. The cache would be keyed on the server's
         * ephemeral Q, but the result also depends on our client private key d which is
         * freshly generated each connection. Caching d1*Q and returning it for d2 (new
         * key, same server Q) produces the wrong master secret → server "bad_record_mac". */
        uint8_t q_bytes[P256_BYTES];
        uint8_t d_be[P256_BYTES];
        uint8_t d_le[P256_BYTES], q_le[P256_BYTES];
        uint8_t shared_le[P256_BYTES], shared_be[P256_BYTES];
        int i, all_zero;

        ret = mbedtls_mpi_write_binary(&Q->MBEDTLS_PRIVATE(X), q_bytes, P256_BYTES);
        if (ret) return ret;
        ret = mbedtls_mpi_write_binary(d, d_be, P256_BYTES);
        if (ret) return ret;

        /* Reverse big-endian → little-endian for donna */
        for (i = 0; i < P256_BYTES; i++) {
            d_le[i] = d_be[P256_BYTES - 1 - i];
            q_le[i] = q_bytes[P256_BYTES - 1 - i];
        }

        ConLog("donna", "start");
        curve25519_donna(shared_le, d_le, q_le);
        ConLog("donna", "end");

        /* Reject all-zero output (low-order point) */
        all_zero = 1;
        for (i = 0; i < P256_BYTES; i++) if (shared_le[i]) { all_zero = 0; break; }
        if (all_zero) return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;

        /* Reverse little-endian → big-endian for mbedTLS MPI */
        for (i = 0; i < P256_BYTES; i++)
            shared_be[i] = shared_le[P256_BYTES - 1 - i];

        return be32_to_mpi(z, shared_be);
    }

    uint8_t pub[P256_BYTES * 2];
    uint8_t priv[P256_BYTES];
    uint8_t secret[P256_BYTES];

    ret = mpi_to_be32(&Q->MBEDTLS_PRIVATE(X), pub);
    if (ret == 0) ret = mpi_to_be32(&Q->MBEDTLS_PRIVATE(Y), pub + P256_BYTES);
    if (ret == 0) ret = mpi_to_be32(d, priv);
    if (ret != 0) goto cleanup;

    /* No shared-secret cache (as on the X25519 path above): the secret is
     * d*Q and d is a fresh key every connection, so a cache keyed on the
     * server's Q returned d1*Q for d2 — every P-256 handshake failed for as
     * long as the server reused its ephemeral key (~60 s at Cloudflare). */
    set_rng(f_rng, p_rng);
    if (!uECC_shared_secret(pub, priv, secret, uECC_secp256r1())) {
        ret = MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
        goto cleanup;
    }

    ret = be32_to_mpi(z, secret);

cleanup:
    memset(priv,   0, sizeof(priv));
    memset(secret, 0, sizeof(secret));
    return ret;
}

/* ------------------------------------------------------------------ */
/* ECDSA: verify signature                                             */
/* P-256: delegates to uECC_verify which uses Shamir's trick          */
/*        (u1*G + u2*Q in one pass, ~2x faster than separate mults).  */
/* Other curves: mbedTLS ecp_muladd fallback.                         */
/* ------------------------------------------------------------------ */
int mbedtls_ecdsa_verify(mbedtls_ecp_group *grp,
                          const unsigned char *buf, size_t blen,
                          const mbedtls_ecp_point *Q,
                          const mbedtls_mpi *r,
                          const mbedtls_mpi *s)
{
    int ret;

    if (grp->id == MBEDTLS_ECP_DP_SECP256R1) {
        /* Pack Q and (r,s) into micro-ecc byte format, call uECC_verify */
        uint8_t pub[P256_BYTES * 2];
        uint8_t sig[P256_BYTES * 2];

        ret = mbedtls_mpi_write_binary(&Q->MBEDTLS_PRIVATE(X), pub,             P256_BYTES);
        if (ret) return ret;
        ret = mbedtls_mpi_write_binary(&Q->MBEDTLS_PRIVATE(Y), pub + P256_BYTES, P256_BYTES);
        if (ret) return ret;
        ret = mbedtls_mpi_write_binary(r, sig,             P256_BYTES);
        if (ret) return ret;
        ret = mbedtls_mpi_write_binary(s, sig + P256_BYTES, P256_BYTES);
        if (ret) return ret;

        /* 2-slot cache check: look for (Q, hash) match in either slot.
         * Slot 0 holds cert-chain verify, slot 1 holds SKE verify.
         * Using 2 slots prevents SKE (different Q, changing hash) from
         * evicting the cert verify entry (constant Q + hash). */
        if (blen <= sizeof(g_ecdsa_cache_hash[0])) {
            int slot;
            for (slot = 0; slot < ECDSA_CACHE_SLOTS; slot++) {
                if (g_ecdsa_cache_valid[slot] &&
                    blen == g_ecdsa_cache_blen[slot] &&
                    memcmp(pub, g_ecdsa_cache_pub[slot],  P256_BYTES * 2) == 0 &&
                    memcmp(buf, g_ecdsa_cache_hash[slot], blen)           == 0) {
                    ConLog("ecdsa", "p256-hit");
                    return 0;   /* cache hit */
                }
            }
        }

        /* Fast path: if the precomputed Shamir2 tables are ready for this Q,
         * use the 4-scalar cached-midpoint Straus verify (OPT #7): u1,u2 are
         * split at bit 128 over (G, Q, G2, Q2), halving the doubling count
         * (~129 vs ~257) for ~30-35% off a full verify.
         * Tables are built for the SKE slot's Q (server cert pub). */
        ConLog("ecdsa", "p256-start");
        int ok;
        if (g_shamir2_ready && memcmp(pub, g_shamir2_Q, P256_BYTES * 2) == 0) {
            ConLog("ecdsa", "p256-fast4");
            ok = uecc_verify_precomp4(buf, (unsigned)blen, sig,
                                      g_shamir2_table, g_shamir2_table2,
                                      uECC_secp256r1());
        } else {
            ok = uECC_verify(pub, buf, (unsigned)blen, sig, uECC_secp256r1());
        }
        ConLog("ecdsa", ok ? "p256-ok" : "p256-fail");

        /* Populate cache on success.
         * Primary key = Q (public key): if a slot for this Q already exists,
         * update it in-place (so cert verify with constant Q persists across
         * connections while SKE verify with the same Q but changing hash also
         * gets its own stable slot).  Only evict a different Q when all slots
         * are full (round-robin). */
        if (ok && blen <= sizeof(g_ecdsa_cache_hash[0])) {
            int slot = -1, i;
            for (i = 0; i < ECDSA_CACHE_SLOTS; i++) {
                if (!g_ecdsa_cache_valid[i]) { slot = i; break; }  /* empty slot */
                if (memcmp(pub, g_ecdsa_cache_pub[i], P256_BYTES * 2) == 0) {
                    slot = i; break;  /* same Q: update in-place */
                }
            }
            if (slot < 0) {
                slot = g_ecdsa_cache_next;  /* all slots occupied, evict oldest */
                g_ecdsa_cache_next = (g_ecdsa_cache_next + 1) % ECDSA_CACHE_SLOTS;
            }
            /* §12 PERF: skip write+dirty if slot already holds identical data */
            if (g_ecdsa_cache_valid[slot] &&
                g_ecdsa_cache_blen[slot] == blen &&
                memcmp(g_ecdsa_cache_hash[slot], buf, blen) == 0 &&
                memcmp(g_ecdsa_cache_pub[slot],  pub, P256_BYTES * 2) == 0) {
                /* identical — nothing to update */
            } else {
                memcpy(g_ecdsa_cache_pub[slot],  pub, P256_BYTES * 2);
                memcpy(g_ecdsa_cache_hash[slot], buf, blen);
                g_ecdsa_cache_blen[slot]  = blen;
                g_ecdsa_cache_valid[slot] = 1;
                g_persist_ecdsa_dirty = 1; /* OPT-3: mark for flush in SslClose */
            }
        }

        return ok ? 0 : MBEDTLS_ERR_ECP_VERIFY_FAILED;
    }

    /* Non-P256 fallback: mbedTLS scalar mults via ecp_muladd */
    ConLog("ecdsa", "fallbk-start");
    {
        mbedtls_mpi e, s_inv, u1, u2;
        mbedtls_ecp_point R;
        size_t n_size   = (grp->nbits + 7) / 8;
        size_t use_size = blen > n_size ? n_size : blen;

        mbedtls_mpi_init(&e);  mbedtls_mpi_init(&s_inv);
        mbedtls_mpi_init(&u1); mbedtls_mpi_init(&u2);
        mbedtls_ecp_point_init(&R);

        if (mbedtls_mpi_cmp_int(r, 1) < 0 ||
            mbedtls_mpi_cmp_mpi(r, &grp->N) >= 0 ||
            mbedtls_mpi_cmp_int(s, 1) < 0 ||
            mbedtls_mpi_cmp_mpi(s, &grp->N) >= 0) {
            ret = MBEDTLS_ERR_ECP_VERIFY_FAILED; goto cleanup;
        }

        MBEDTLS_MPI_CHK(mbedtls_mpi_read_binary(&e, buf, use_size));
        if (use_size * 8 > grp->nbits)
            MBEDTLS_MPI_CHK(mbedtls_mpi_shift_r(&e, use_size * 8 - grp->nbits));
        if (mbedtls_mpi_cmp_mpi(&e, &grp->N) >= 0)
            MBEDTLS_MPI_CHK(mbedtls_mpi_sub_mpi(&e, &e, &grp->N));

        MBEDTLS_MPI_CHK(mbedtls_mpi_inv_mod(&s_inv, s, &grp->N));
        MBEDTLS_MPI_CHK(mbedtls_mpi_mul_mpi(&u1, &e, &s_inv));
        MBEDTLS_MPI_CHK(mbedtls_mpi_mod_mpi(&u1, &u1, &grp->N));
        MBEDTLS_MPI_CHK(mbedtls_mpi_mul_mpi(&u2, r,  &s_inv));
        MBEDTLS_MPI_CHK(mbedtls_mpi_mod_mpi(&u2, &u2, &grp->N));
        MBEDTLS_MPI_CHK(mbedtls_ecp_muladd(grp, &R, &u1, &grp->G, &u2, Q));

        if (mbedtls_ecp_is_zero(&R)) {
            ret = MBEDTLS_ERR_ECP_VERIFY_FAILED; goto cleanup;
        }
        MBEDTLS_MPI_CHK(mbedtls_mpi_mod_mpi(&R.MBEDTLS_PRIVATE(X),
                                              &R.MBEDTLS_PRIVATE(X), &grp->N));
        ret = mbedtls_mpi_cmp_mpi(&R.MBEDTLS_PRIVATE(X), r) == 0
              ? 0 : MBEDTLS_ERR_ECP_VERIFY_FAILED;

cleanup:
        mbedtls_ecp_point_free(&R);
        mbedtls_mpi_free(&e); mbedtls_mpi_free(&s_inv);
        mbedtls_mpi_free(&u1); mbedtls_mpi_free(&u2);
        ConLog("ecdsa", ret == 0 ? "fallbk-ok" : "fallbk-fail");
        return ret;
    }
}
