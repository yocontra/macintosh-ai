#pragma once
/* tls_cache.h — TLS acceleration caches (DNS, ECDSA, session tickets).
   Boilerplate — do not modify. These speed up TLS connection time:
     · DNS cache    — avoids repeated DNS lookups (~200ms saved)
     · ECDSA cache  — caches certificate verify results (~1–2s saved)
     · TLS session  — resumes previous TLS session (~3s saved)
   Loaded/saved automatically by prefs_io.cpp alongside AppSettings.  */

/* ------------------------------------------------------------------ */
/* DNS cache                                                           */
/* ------------------------------------------------------------------ */
struct CachedDNS {
    unsigned long ip;
    unsigned long secs_written;  /* GetDateTime() when last cached: real
                                    time, unlike TickCount() which restarts
                                    at every boot and made an old entry
                                    look fresh                          */
};
void LoadDNSCache(CachedDNS &out);
void SaveDNSCache(const CachedDNS &in);

/* ------------------------------------------------------------------ */
/* Persistent ECDSA verify cache                                       */
/* ------------------------------------------------------------------ */
#define PERSIST_ECDSA_SLOTS 2
struct PersistentECDSACacheEntry {
    unsigned char q_pub[64];
    unsigned char msg_hash[32];
    unsigned char hash_len;
    unsigned char valid;
    unsigned char pad[2];
};
void LoadPersistECDSACache(PersistentECDSACacheEntry slots[PERSIST_ECDSA_SLOTS]);
void SavePersistECDSACache(const PersistentECDSACacheEntry slots[PERSIST_ECDSA_SLOTS]);

/* ------------------------------------------------------------------ */
/* EC midpoint cache — 4-scalar Straus verify (OPT #7)                 */
/* Q2 = 2^128·Q computed once per server cert (128 Jacobian doublings, */
/* ~0.5s on SE/30) and persisted so restarts skip the recompute.       */
/* ------------------------------------------------------------------ */
struct PersistedECMidpoint {
    unsigned char q_pub[64];     /* big-endian X|Y the midpoint belongs to */
    unsigned char q2_native[64]; /* 2^128·Q, raw uECC native word format   */
    unsigned char valid;
    unsigned char pad[3];
};
void LoadECMidpoint(PersistedECMidpoint &out);
void SaveECMidpoint(const PersistedECMidpoint &in);

/* ------------------------------------------------------------------ */
/* TLS session ticket persistence                                      */
/* ------------------------------------------------------------------ */
/* Serialized via mbedtls_ssl_session_save: hand-copying master+ticket
   dropped session fields the resumed handshake needs, so cross-restart
   resume was always rejected (proven by ticket-fingerprint logs, Jun 2026).
   Blob includes the peer cert (MBEDTLS_SSL_KEEP_PEER_CERTIFICATE), hence
   the 2 KB ceiling. Struct is last in the prefs layout, so growing it
   shifts no other offsets; the layout word bump wipes stale entries. */
enum { kTLSBlobMax = 2048 };
struct PersistedTLSSession {
    unsigned long  magic;
    unsigned short blob_len;        /* bytes used in blob[] */
    unsigned short pad;
    unsigned long  saved_datetime;
    unsigned char  blob[kTLSBlobMax];
};
void SaveTLSSession(const PersistedTLSSession& sess);
bool LoadTLSSession(PersistedTLSSession& sess);

/* ------------------------------------------------------------------ */
/* Random seed                                                         */
/* ------------------------------------------------------------------ */
/* 32 bytes from the random generator, saved at every launch and mixed
   into the next launch's seeding, so each launch builds on the timing
   jitter of all the earlier ones (see mac_entropy.c). Stored after the
   TLS session and left out of the layout word: an older prefs file just
   has no seed yet, and its caches stay valid. */
enum { kRandomSeedLen = 32 };
struct PersistedRandomSeed {
    unsigned long magic;            /* 'Seed' = 0x53656564 */
    unsigned char seed[kRandomSeedLen];
};
bool LoadRandomSeed(unsigned char seed[kRandomSeedLen]);   /* false = none */
void SaveRandomSeed(const unsigned char seed[kRandomSeedLen]);

/* ------------------------------------------------------------------ */
/* C-callable wrappers (used by uecc_alt.c)                           */
/* ------------------------------------------------------------------ */
#ifdef __cplusplus
extern "C" {
#endif
void LoadPersistECDSASlot_c(int slot,
                             unsigned char q_pub_out[64],
                             unsigned char hash_out[32],
                             unsigned char *hash_len_out,
                             unsigned char *valid_out);
void SavePersistECDSASlot_c(int slot,
                             const unsigned char q_pub_in[64],
                             const unsigned char hash_in[32],
                             unsigned char hash_len_in);
void SaveAllPersistECDSASlots_c(const unsigned char *q_pub_flat,
                                 const unsigned char *hash_flat,
                                 const unsigned char *hash_lens,
                                 const unsigned char *valids,
                                 int num_slots);
void LoadAllPersistECDSASlots_c(unsigned char *q_pub_flat,
                                 unsigned char *hash_flat,
                                 unsigned char *hash_lens,
                                 unsigned char *valids,
                                 int num_slots);
void LoadECMidpoint_c(unsigned char q_pub_out[64],
                       unsigned char q2_native_out[64],
                       unsigned char *valid_out);
void SaveECMidpoint_c(const unsigned char q_pub_in[64],
                       const unsigned char q2_native_in[64]);
#ifdef __cplusplus
}
#endif
