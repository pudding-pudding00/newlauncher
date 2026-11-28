/* SHA-256 and ECDSA P-256 verification over PSA (the P-256 accelerator hooks
 * into the PSA verify path, so this stays free of hand-written crypto). */
#include <string.h>

#include <mbedtls/psa_util.h>
#include <psa/crypto.h>
#include <wiitls/sig.h>

#include "wtls_internal.h"

_Static_assert(sizeof(psa_hash_operation_t) <= sizeof(((wtls_sha256_ctx *)0)->opaque),
               "wtls_sha256_ctx is too small for psa_hash_operation_t");
_Static_assert(_Alignof(psa_hash_operation_t) <= _Alignof(wtls_sha256_ctx),
               "wtls_sha256_ctx is not aligned enough for psa_hash_operation_t");

#define P256_POINT_LEN 65u
#define P256_SIG_RAW_LEN 64u
#define P256_SIG_DER_MAX 72u

/* SubjectPublicKeyInfo of an id-ecPublicKey / prime256v1 key up to the point:
 * SEQ(0x59) { SEQ(0x13) { OID ecPublicKey, OID prime256v1 }, BIT STRING(0x42, 0 unused) }. */
static const uint8_t k_spki_prefix[26] = {
    0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01,
    0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00};

static const uint8_t *point_of(const uint8_t *pub, size_t len) {
    if (len == P256_POINT_LEN && pub[0] == 0x04) return pub;
    if (len == sizeof k_spki_prefix + P256_POINT_LEN && memcmp(pub, k_spki_prefix, sizeof k_spki_prefix) == 0 &&
        pub[sizeof k_spki_prefix] == 0x04)
        return &pub[sizeof k_spki_prefix];
    return NULL;
}

/* Fills raw[64]; DER input must be canonical (it is re-encoded and compared). */
static wtls_status sig_to_raw(const uint8_t *sig, size_t sig_len, wtls_sig_format fmt, uint8_t raw[64]) {
    if (fmt == WTLS_SIG_RAW) {
        if (sig_len != P256_SIG_RAW_LEN) return WTLS_ERR_ARG;
        memcpy(raw, sig, P256_SIG_RAW_LEN);
        return WTLS_OK;
    }
    if (fmt != WTLS_SIG_DER || sig_len < 8 || sig_len > P256_SIG_DER_MAX) return WTLS_ERR_ARG;
    size_t rl = 0;
    if (mbedtls_ecdsa_der_to_raw(256, sig, sig_len, raw, P256_SIG_RAW_LEN, &rl) != 0 || rl != P256_SIG_RAW_LEN)
        return WTLS_ERR_INTEGRITY;
    uint8_t again[P256_SIG_DER_MAX + 8];
    size_t al = 0;
    if (mbedtls_ecdsa_raw_to_der(256, raw, P256_SIG_RAW_LEN, again, sizeof again, &al) != 0 || al != sig_len ||
        memcmp(again, sig, sig_len) != 0)
        return WTLS_ERR_INTEGRITY; /* BER quirks, padding, trailing bytes */
    return WTLS_OK;
}

wtls_status wtls_sig_verify_p256_digest(const uint8_t *pubkey, size_t pubkey_len, const uint8_t sha256[32],
                                        const uint8_t *sig, size_t sig_len, wtls_sig_format fmt) {
    if (!pubkey || !sha256 || !sig) return WTLS_ERR_ARG;
    if (!wtls_core_initialized()) return WTLS_ERR_STATE;
    const uint8_t *point = point_of(pubkey, pubkey_len);
    if (!point) return WTLS_ERR_ARG;
    uint8_t raw[P256_SIG_RAW_LEN];
    wtls_status st = sig_to_raw(sig, sig_len, fmt, raw);
    if (st != WTLS_OK) return st;

    psa_key_attributes_t a = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&a, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_usage_flags(&a, PSA_KEY_USAGE_VERIFY_HASH);
    psa_set_key_algorithm(&a, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_key_id_t id = 0;
    psa_status_t ps = psa_import_key(&a, point, P256_POINT_LEN, &id);
    if (ps != PSA_SUCCESS) return ps == PSA_ERROR_INSUFFICIENT_MEMORY ? WTLS_ERR_NOMEM : WTLS_ERR_ARG; /* not on the curve */
    ps = psa_verify_hash(id, PSA_ALG_ECDSA(PSA_ALG_SHA_256), sha256, 32, raw, sizeof raw);
    psa_destroy_key(id);
    if (ps == PSA_SUCCESS) return WTLS_OK;
    if (ps == PSA_ERROR_INVALID_SIGNATURE) return WTLS_ERR_INTEGRITY;
    return ps == PSA_ERROR_INSUFFICIENT_MEMORY ? WTLS_ERR_NOMEM : WTLS_ERR_INTERNAL;
}

wtls_status wtls_sig_verify_p256(const uint8_t *pubkey, size_t pubkey_len, const uint8_t *msg, size_t msg_len,
                                 const uint8_t *sig, size_t sig_len, wtls_sig_format fmt) {
    if ((!msg && msg_len) || !pubkey || !sig) return WTLS_ERR_ARG;
    if (!wtls_core_initialized()) return WTLS_ERR_STATE;
    uint8_t digest[32];
    size_t dl = 0;
    static const uint8_t k_none = 0;
    psa_status_t ps = psa_hash_compute(PSA_ALG_SHA_256, msg ? msg : &k_none, msg_len, digest, sizeof digest, &dl);
    if (ps != PSA_SUCCESS || dl != sizeof digest) return WTLS_ERR_INTERNAL;
    return wtls_sig_verify_p256_digest(pubkey, pubkey_len, digest, sig, sig_len, fmt);
}

static psa_hash_operation_t *op_of(wtls_sha256_ctx *c) { return (psa_hash_operation_t *)(void *)c->opaque; }

wtls_status wtls_sha256_init(wtls_sha256_ctx *c) {
    if (!c) return WTLS_ERR_ARG;
    if (!wtls_core_initialized()) return WTLS_ERR_STATE;
    psa_hash_operation_t fresh = PSA_HASH_OPERATION_INIT;
    memset(c, 0, sizeof *c);
    memcpy(c->opaque, &fresh, sizeof fresh);
    psa_status_t ps = psa_hash_setup(op_of(c), PSA_ALG_SHA_256);
    return ps == PSA_SUCCESS ? WTLS_OK : (ps == PSA_ERROR_INSUFFICIENT_MEMORY ? WTLS_ERR_NOMEM : WTLS_ERR_INTERNAL);
}

wtls_status wtls_sha256_update(wtls_sha256_ctx *c, const void *p, size_t n) {
    if (!c || (!p && n)) return WTLS_ERR_ARG;
    if (n == 0) return WTLS_OK;
    return psa_hash_update(op_of(c), p, n) == PSA_SUCCESS ? WTLS_OK : WTLS_ERR_INTERNAL;
}

wtls_status wtls_sha256_finish(wtls_sha256_ctx *c, uint8_t out[32]) {
    if (!c || !out) return WTLS_ERR_ARG;
    size_t l = 0;
    psa_status_t ps = psa_hash_finish(op_of(c), out, 32, &l);
    return ps == PSA_SUCCESS && l == 32 ? WTLS_OK : WTLS_ERR_INTERNAL;
}

void wtls_sha256_abort(wtls_sha256_ctx *c) {
    if (c) psa_hash_abort(op_of(c));
}
