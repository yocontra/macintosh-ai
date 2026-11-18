/*
 * RootCert.h - Embedded Root CA Certificate for api.notion.com
 *
 * Certificate: GTS Root R4
 * Subject:     C=US, O=Google Trust Services LLC, CN=GTS Root R4
 * Expires:     Jun 22 00:00:00 2036 GMT
 *
 * Chain of trust for api.notion.com:
 *   notion.com  (leaf, ECDSA)
 *     -> Google Trust Services WE1  (intermediate, ECDSA)
 *       -> GTS Root R4  (root, ECDSA)
 */

#pragma once
#include <stddef.h>

// §4 MEMORY: extern declarations — definitions are in RootCert.cpp (one copy only)
extern const unsigned char kGlobalSignRootCA_DER[];
extern const size_t        kGlobalSignRootCA_DER_len;

/*
 * Google Trust Services WE1 intermediate CA
 * Subject:  C=US, O=Google Trust Services, CN=WE1
 * Issuer:   GTS Root R4 (secp384r1 — slow on 68K)
 * Expires:  Feb 20 14:00:00 2029 GMT
 *
 * Including this as a trusted cert lets mbedTLS stop chain verification here,
 * so only the leaf cert's P-256 signature (fast via micro-ecc) is checked,
 * avoiding the slow P-384 verification of WE1 against GTS Root R4.
 */
extern const unsigned char kGoogleWE1_DER[];
extern const size_t        kGoogleWE1_DER_len;
