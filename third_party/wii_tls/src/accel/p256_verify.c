/* NIST P-256 (secp256r1) ECDSA verify over eight 32-bit limbs.
 *
 * Field arithmetic: canonical limbs (each fe is in [0, p) at the boundaries).
 * fe_mul/fe_sq accumulate the 64/36 schoolbook products into int64_t columns
 * split at 32 bits (t[k] += lo, t[k+1] += hi; every column stays < 2^38), then
 * one Solinas pass folds the upper half with 2^256 = 2^224 - 2^192 - 2^96 + 1
 * (mod p) - all three shifts are whole limbs, so the fold is four add/subs per
 * upper word - and a short cancel loop folds the remaining top word. All
 * intermediate values are exact integers in int64_t words; the bounds are
 * argued inline. The 32x32->64 products are mullw/mulhwu on the Broadway; no
 * 64-bit multiplier is needed anywhere.
 *
 * Scalar arithmetic mod n: shift-and-add mul (one conditional subtract per
 * bit) and a binary extended GCD for s^-1; both are far cheaper than the
 * ~2900 field multiplications of the point arithmetic, so their simplicity
 * costs nothing measurable.
 *
 * Point arithmetic: Jacobian with a = -3. Doubling is the 3M+5S variant of
 * dbl-2001-b (M = 3(X+Z^2)(X-Z^2), Z3 = (Y+Z)^2 - Y^2 - Z^2); mixed addition
 * follows madd-2007-bl (8M+3S), with the equal/opposite-point cases handled
 * as branches (public inputs). The verify equation R = u1*G + u2*Q runs as
 * one interleaved wNAF loop (Shamir's trick): w = 6 digits for G against the
 * generated affine table k_p256_g_odd (1 KiB in .rodata), w = 5 digits for Q
 * against eight odd multiples normalised affinely with a single mod-p
 * inversion (Montgomery batch inversion). The final comparison is projective:
 * X == r*Z^2 (mod p), else, only when r + n < p, X == (r+n)*Z^2 - no field
 * inversion for the result.
 *
 * Variable time by design (public inputs only). */
#include <stdint.h>
#include <string.h>

#include "accel/wtls_p256.h"
#include "accel/p256_curve.inc"

typedef uint32_t fe[8]; /* little-endian limbs; canonical mod p at the API edges */

/* ------------------------------------------------------------------ */
/* 256-bit word helpers shared by the field (mod p) and scalar (mod n) */
/* ------------------------------------------------------------------ */

static int fe_cmp(const fe a, const fe b) {
    for (int i = 7; i >= 0; i--) {
        if (a[i] != b[i]) {
            return a[i] < b[i] ? -1 : 1;
        }
    }
    return 0;
}

static void fe_zero(fe r) { memset(r, 0, sizeof(fe)); }

static int fe_is_zero(const fe a) {
    uint32_t or_ = 0;
    for (int i = 0; i < 8; i++) {
        or_ |= a[i];
    }
    return or_ == 0;
}

static int fe_is_one(const fe a) {
    uint32_t or_ = 0;
    for (int i = 1; i < 8; i++) {
        or_ |= a[i];
    }
    return a[0] == 1 && or_ == 0;
}

/* Wire format (big-endian, most significant byte first) to limbs. */
static void fe_from_be(fe r, const uint8_t *in) {
    for (int i = 0; i < 8; i++) {
        const uint8_t *w = in + 28 - 4 * i;
        r[i] = ((uint32_t)w[0] << 24) | ((uint32_t)w[1] << 16) |
               ((uint32_t)w[2] << 8) | (uint32_t)w[3];
    }
}

/* Raw word adds/subs; the caller handles the modulus. */
static int sc_add_raw(fe r, const fe a, const fe b) {
    uint32_t c = 0;
    for (int i = 0; i < 8; i++) {
        uint64_t s = (uint64_t)a[i] + b[i] + c;
        r[i] = (uint32_t)s;
        c = (uint32_t)(s >> 32);
    }
    return (int)c;
}

static int sc_sub_raw(fe r, const fe a, const fe b) {
    uint32_t borrow = 0;
    for (int i = 0; i < 8; i++) {
        uint64_t d = (uint64_t)a[i] - b[i] - borrow;
        r[i] = (uint32_t)d;
        borrow = (uint32_t)((d >> 32) & 1);
    }
    return (int)borrow;
}

static void sc_shr1(fe r, const fe a) {
    for (int i = 0; i < 8; i++) {
        r[i] = a[i] >> 1;
        if (i < 7) {
            r[i] |= (a[i + 1] & 1) << 31;
        }
    }
}

/* r = (a + b) mod m; a, b in [0, m). One conditional subtract (a+b < 2m). */
static void add_mod(fe r, const fe a, const fe b, const fe m) {
    uint32_t c = (uint32_t)sc_add_raw(r, a, b);
    if (c != 0 || fe_cmp(r, m) >= 0) {
        /* the borrow out cancels the carry: the full value was r + c*2^256 */
        sc_sub_raw(r, r, m);
    }
}

/* r = (a - b) mod m; a, b in [0, m). */
static void sub_mod(fe r, const fe a, const fe b, const fe m) {
    if (sc_sub_raw(r, a, b) != 0) {
        /* a - b + m < m < 2^256: no carry can escape */
        sc_add_raw(r, r, m);
    }
}

/* r = a/2 mod m for even a in [0, m), m odd (the x-updates of the
 * binary extended GCD). */
static void halve_mod(fe r, const fe a, const fe m) {
    fe t;
    int carry = 0;
    if (a[0] & 1) {
        carry = sc_add_raw(t, a, m);
    } else {
        memcpy(t, a, sizeof(fe));
    }
    for (int i = 0; i < 8; i++) {
        uint32_t hi = (i < 7) ? ((t[i + 1] & 1u) << 31) : ((uint32_t)carry << 31);
        r[i] = (t[i] >> 1) | hi;
    }
}

/* r = a^-1 mod m for a in [1, m), m odd. Binary extended GCD, maintaining
 * a*x1 = u and a*x2 = v (mod m) with u, v shrinking until one is 1.
 * Variable time - public inputs only. */
static void inv_mod(fe r, const fe a, const fe m) {
    fe u, v, x1, x2;
    memcpy(u, a, sizeof(fe));
    memcpy(v, m, sizeof(fe));
    fe_zero(x1);
    x1[0] = 1;
    fe_zero(x2);
    while (!fe_is_one(u) && !fe_is_one(v)) {
        while (!(u[0] & 1)) {
            sc_shr1(u, u);
            halve_mod(x1, x1, m);
        }
        while (!(v[0] & 1)) {
            sc_shr1(v, v);
            halve_mod(x2, x2, m);
        }
        if (fe_cmp(u, v) >= 0) {
            sub_mod(u, u, v, m);
            sub_mod(x1, x1, x2, m);
        } else {
            sub_mod(v, v, u, m);
            sub_mod(x2, x2, x1, m);
        }
    }
    memcpy(r, fe_is_one(u) ? x1 : x2, sizeof(fe));
}

/* r = a*b mod m; a, b in [0, m). MSB-first double-and-add: one mod-add per
 * bit plus one for each set bit of b. */
static void mul_mod(fe r, const fe a, const fe b, const fe m) {
    fe t;
    fe_zero(t);
    for (int i = 255; i >= 0; i--) {
        add_mod(t, t, t, m);
        if ((b[i >> 5] >> (i & 31)) & 1) {
            add_mod(t, t, a, m);
        }
    }
    memcpy(r, t, sizeof(fe));
}

/* ------------------------------------------------------------------ */
/* Field arithmetic mod p (canonical [0, p))                           */
/* ------------------------------------------------------------------ */

/* r = (a + b) mod p. */
static void fe_add(fe r, const fe a, const fe b) { add_mod(r, a, b, k_p256_p); }

/* r = (a - b) mod p. */
static void fe_sub(fe r, const fe a, const fe b) { sub_mod(r, a, b, k_p256_p); }

/* Reduce the exact 512-bit product words (int64 columns t[k] < 2^38) to
 * canonical [0, p). */
static void fe_reduce(fe r, const int64_t t[16]) {
    /* carry-normalise to the exact 16-word product; the final carry is 0
     * because the product is < 2^512 */
    int64_t w[16];
    int64_t carry = 0;
    for (int k = 0; k < 16; k++) {
        int64_t v = t[k] + carry;
        w[k] = v & 0xffffffff;
        carry = v >> 32;
    }

    /* Solinas fold, top-down: word k (k = 15..8) has weight
     * 2^256 * 2^(32(k-8)), and 2^256 = 2^224 - 2^192 - 2^96 + 1 (mod p), so
     * folding it adds +v at word k-1, -v at k-2, -v at k-5 and +v at k-8.
     * Processing from the top down lets every word absorb all higher
     * contributions before it is folded itself, so w[8..15] are zero
     * afterwards. Every word stays below ~2^41 in absolute value. */
    for (int k = 15; k >= 8; k--) {
        int64_t v = w[k];
        if (v != 0) {
            w[k] = 0;
            w[k - 1] += v;
            w[k - 2] -= v;
            w[k - 5] -= v;
            w[k - 8] += v;
        }
    }

    /* w[0..7] is congruent to the product mod p; as a signed integer it is
     * within ~2^41 * 2^256 of zero. Fold the top word: each iteration
     * subtracts top*p from the value (exactly), and since the top shrinks by
     * 2^32 per iteration, at most three iterations leave the value in
     * [0, 2^256). */
    carry = 0;
    for (int j = 0; j < 8; j++) {
        int64_t v = w[j] + carry;
        w[j] = v & 0xffffffff;
        carry = v >> 32;
    }
    while (carry != 0) {
        w[7] += carry;   /* + top * 2^224 */
        w[6] -= carry;   /* - top * 2^192 */
        w[3] -= carry;   /* - top * 2^96  */
        w[0] += carry;   /* + top         */
        carry = 0;
        for (int j = 0; j < 8; j++) {
            int64_t v = w[j] + carry;
            w[j] = v & 0xffffffff;
            carry = v >> 32;
        }
    }

    /* value in [0, 2^256): one conditional subtract of p */
    for (int j = 0; j < 8; j++) {
        r[j] = (uint32_t)w[j];
    }
    if (fe_cmp(r, k_p256_p) >= 0) {
        sc_sub_raw(r, r, k_p256_p);
    }
}

/* r = a*b mod p: 64 products into int64 columns, hi split at 32 bits. */
static void fe_mul(fe r, const fe a, const fe b) {
    int64_t t[16] = {0};
    for (int i = 0; i < 8; i++) {
        uint64_t ai = a[i];
        for (int j = 0; j < 8; j++) {
            uint64_t p = ai * b[j];
            t[i + j] += (uint32_t)p;
            t[i + j + 1] += p >> 32;
        }
    }
    fe_reduce(r, t);
}

/* r = a^2 mod p: 36 products, off-diagonal ones doubled. */
static void fe_sq(fe r, const fe a) {
    int64_t t[16] = {0};
    for (int i = 0; i < 8; i++) {
        uint64_t p = (uint64_t)a[i] * a[i];
        t[2 * i] += (uint32_t)p;
        t[2 * i + 1] += p >> 32;
    }
    for (int i = 0; i < 8; i++) {
        for (int j = i + 1; j < 8; j++) {
            uint64_t p = (uint64_t)a[i] * a[j];
            uint32_t lo = (uint32_t)p, hi = (uint32_t)(p >> 32);
            t[i + j] += 2 * (uint64_t)lo;
            t[i + j + 1] += 2 * (uint64_t)hi;
        }
    }
    fe_reduce(r, t);
}

/* r = 3*a mod p (small multiplies as adds; alias-safe). */
static void fe_mul3(fe r, const fe a) {
    fe t;
    fe_add(t, a, a);
    fe_add(r, t, a);
}

/* ------------------------------------------------------------------ */
/* Jacobian points (X, Y, Z): (X/Z^2, Y/Z^3); Z == 0 is the identity.  */
/* ------------------------------------------------------------------ */

typedef struct {
    fe x, y, z;
} pt;

static void pt_set_identity(pt *p) {
    fe_zero(p->x);
    fe_zero(p->y);
    fe_zero(p->z);
}

static int pt_is_identity(const pt *p) { return fe_is_zero(p->z); }

static void pt_set_affine(pt *p, const fe x, const fe y) {
    memcpy(p->x, x, sizeof(fe));
    memcpy(p->y, y, sizeof(fe));
    fe_zero(p->z);
    p->z[0] = 1;
}

/* R = 2P, a = -3: the 3M+5S variant of dbl-2001-b
 *   M = 3(X+Z^2)(X-Z^2), S = 4XY^2, U = 8Y^4, T = M^2-2S,
 *   X' = T, Y' = M(S-T)-U, Z' = (Y+Z)^2 - Y^2 - Z^2. */
static void pt_dbl(pt *r, const pt *p) {
    if (pt_is_identity(p)) {
        pt_set_identity(r);
        return;
    }
    fe z2, b2, t2, m, s, u, t, x3, y3, z3, tmp;
    fe_sq(z2, p->z);                                    /* Z^2 */
    fe_add(tmp, p->x, z2);
    fe_sub(t, p->x, z2);
    fe_mul(m, tmp, t);
    fe_mul3(m, m);                                      /* M */
    fe_sq(b2, p->y);                                    /* Y^2 */
    fe_add(t2, b2, b2);                                 /* 2Y^2 */
    fe_mul(s, p->x, t2);
    fe_add(s, s, s);                                    /* S = 4XY^2 */
    fe_sq(u, t2);
    fe_add(u, u, u);                                    /* U = 2(2Y^2)^2 = 8Y^4 */
    fe_sq(t, m);                                        /* M^2 */
    fe_sub(t, t, s);
    fe_sub(t, t, s);                                    /* T = M^2 - 2S */
    memcpy(x3, t, sizeof(fe));
    fe_sub(s, s, t);
    fe_mul(s, s, m);
    fe_sub(y3, s, u);                                   /* M(S-T) - U */
    fe_add(tmp, p->y, p->z);
    fe_sq(z3, tmp);
    fe_sub(z3, z3, b2);
    fe_sub(z3, z3, z2);                                 /* 2YZ */
    memcpy(r->x, x3, sizeof(fe));
    memcpy(r->y, y3, sizeof(fe));
    memcpy(r->z, z3, sizeof(fe));
}

/* R = P + Q with Q affine (Z2 = 1): madd-2007-bl (8M+3S). */
static void pt_madd(pt *r, const pt *p, const fe qx, const fe qy) {
    if (pt_is_identity(p)) {
        memcpy(r->x, qx, sizeof(fe));
        memcpy(r->y, qy, sizeof(fe));
        fe_zero(r->z);
        r->z[0] = 1;
        return;
    }
    fe z1z1, u2, s2, h, rr, hh, hhh, v, t, x3, y3, z3;
    fe_sq(z1z1, p->z);
    fe_mul(u2, qx, z1z1);                                /* x2*Z1^2 */
    fe_mul(s2, z1z1, p->z);
    fe_mul(s2, qy, s2);                                  /* y2*Z1^3 */
    fe_sub(h, u2, p->x);
    fe_sub(rr, s2, p->y);
    if (fe_is_zero(h)) {
        if (fe_is_zero(rr)) {
            pt_dbl(r, p); /* P == Q */
        } else {
            pt_set_identity(r); /* P == -Q */
        }
        return;
    }
    fe_mul(z3, p->z, h);                                 /* Z1*H */
    fe_sq(hh, h);
    fe_mul(hhh, hh, h);
    fe_mul(v, p->x, hh);
    fe_sq(x3, rr);
    fe_add(t, v, v);
    fe_sub(x3, x3, t);
    fe_sub(x3, x3, hhh);                                 /* R^2 - 2V - H^3 */
    fe_sub(t, v, x3);
    fe_mul(t, t, rr);
    fe_mul(hhh, p->y, hhh);
    fe_sub(y3, t, hhh);                                 /* R(V-X3) - Y1*H^3 */
    memcpy(r->x, x3, sizeof(fe));
    memcpy(r->y, y3, sizeof(fe));
    memcpy(r->z, z3, sizeof(fe));
}

/* R = P + Q, both Jacobian (jadd-2007-bl); only used to build the Q table. */
static void pt_jadd(pt *r, const pt *p, const pt *q) {
    if (pt_is_identity(p)) {
        *r = *q;
        return;
    }
    if (pt_is_identity(q)) {
        *r = *p;
        return;
    }
    fe z1z1, z2z2, u1, u2, s1, s2, h, rr, hh, hhh, v, t, x3, y3, z3;
    fe_sq(z1z1, p->z);
    fe_sq(z2z2, q->z);
    fe_mul(u1, p->x, z2z2);
    fe_mul(u2, q->x, z1z1);
    fe_mul(s1, p->y, z2z2);
    fe_mul(s1, s1, q->z);
    fe_mul(s2, q->y, z1z1);
    fe_mul(s2, s2, p->z);
    fe_sub(h, u2, u1);
    fe_sub(rr, s2, s1);
    if (fe_is_zero(h)) {
        if (fe_is_zero(rr)) {
            pt_dbl(r, p);
        } else {
            pt_set_identity(r);
        }
        return;
    }
    fe_sq(hh, h);
    fe_mul(hhh, hh, h);
    fe_mul(v, u1, hh);
    fe_sq(x3, rr);
    fe_add(t, v, v);
    fe_sub(x3, x3, t);
    fe_sub(x3, x3, hhh);
    fe_sub(t, v, x3);
    fe_mul(t, t, rr);
    fe_mul(hhh, s1, hhh);
    fe_sub(y3, t, hhh);
    /* Z3 = Z1*Z2*H: the scaling consistent with X3/Y3 above (with the
     * (Z1+Z2)^2 trick the coordinates would need the 4/2-rescaled variant). */
    fe_mul(z3, p->z, q->z);
    fe_mul(z3, z3, h);
    memcpy(r->x, x3, sizeof(fe));
    memcpy(r->y, y3, sizeof(fe));
    memcpy(r->z, z3, sizeof(fe));
}

/* ------------------------------------------------------------------ */
/* wNAF                                                               */
/* ------------------------------------------------------------------ */

/* digits[i] = k_i in window w (nonzero digits odd, |d| <= 2^(w-1)-1);
 * returns the digit count. k < 2^256 - 2^224 keeps the +/-d in word 0. */
static void sc_add_small(fe r, const fe a, uint32_t v) {
    uint32_t carry = v;
    for (int i = 0; i < 8; i++) {
        uint64_t s = (uint64_t)a[i] + carry;
        r[i] = (uint32_t)s;
        carry = (uint32_t)(s >> 32);
    }
}

static void sc_sub_small(fe r, const fe a, uint32_t v) {
    uint32_t borrow = v;
    for (int i = 0; i < 8; i++) {
        uint64_t d = (uint64_t)a[i] - borrow;
        r[i] = (uint32_t)d;
        borrow = (uint32_t)((d >> 32) & 1);
    }
}

static int sc_wnaf(int8_t digits[257], const fe k, int w) {
    fe t;
    memcpy(t, k, sizeof(fe));
    int i = 0;
    while (!fe_is_zero(t)) {
        int8_t d = 0;
        if (t[0] & 1) {
            int32_t dd = (int32_t)(t[0] & (uint32_t)((1 << w) - 1));
            if (dd > (1 << (w - 1))) {
                dd -= 1 << w;
            }
            if (dd > 0) {
                sc_sub_small(t, t, (uint32_t)dd);
            } else {
                sc_add_small(t, t, (uint32_t)(-dd));
            }
            d = (int8_t)dd;
        }
        digits[i++] = d;
        sc_shr1(t, t);
    }
    for (; i < 257; i++) {
        digits[i] = 0;
    }
    return i;
}

/* ------------------------------------------------------------------ */
/* Verify                                                             */
/* ------------------------------------------------------------------ */

/* tab[i] = (2i+1)Q affine (wNAF w = 5 digits), normalised with one mod-p
 * inversion (Montgomery batch inversion over the Z coordinates). */
static void build_q_table(fe tab_x[8], fe tab_y[8], const fe qx, const fe qy) {
    pt t[8], m2;
    pt_set_affine(&t[0], qx, qy);
    pt_dbl(&m2, &t[0]); /* 2Q */
    for (int i = 1; i < 8; i++) {
        pt_jadd(&t[i], &t[i - 1], &m2); /* (2i+1)Q */
    }
    fe z[8], pre[8], u, zi, zi2, zi3;
    for (int i = 0; i < 8; i++) {
        memcpy(z[i], t[i].z, sizeof(fe));
    }
    memcpy(pre[0], z[0], sizeof(fe));
    for (int i = 1; i < 8; i++) {
        fe_mul(pre[i], pre[i - 1], z[i]);
    }
    inv_mod(u, pre[7], k_p256_p);
    for (int i = 7; i >= 0; i--) {
        if (i > 0) {
            fe_mul(zi, u, pre[i - 1]);
        } else {
            memcpy(zi, u, sizeof(fe));
        }
        fe_mul(u, u, z[i]);
        fe_sq(zi2, zi);
        fe_mul(zi3, zi2, zi);
        fe_mul(tab_x[i], t[i].x, zi2);
        fe_mul(tab_y[i], t[i].y, zi3);
    }
}

/* Validity of Q as a curve point: coordinates in range and on the curve. */
static int fe_on_curve(const fe x, const fe y) {
    fe lhs, rhs, t;
    fe_sq(lhs, y);
    fe_sq(rhs, x);
    fe_mul(rhs, rhs, x); /* x^3 */
    fe_mul3(t, x);
    fe_sub(rhs, rhs, t);
    fe_add(rhs, rhs, k_p256_b);
    return fe_cmp(lhs, rhs) == 0;
}

psa_status_t wtls_p256_verify(const uint8_t *pub, const uint8_t *hash,
                              size_t hash_len, const uint8_t *sig) {
    if (pub == NULL || hash == NULL || sig == NULL || hash_len == 0 ||
        pub[0] != 0x04) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    fe qx, qy;
    fe_from_be(qx, pub + 1);
    fe_from_be(qy, pub + 33);
    if (fe_cmp(qx, k_p256_p) >= 0 || fe_cmp(qy, k_p256_p) >= 0 ||
        !fe_on_curve(qx, qy)) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    fe r, s;
    fe_from_be(r, sig);
    fe_from_be(s, sig + 32);
    if (fe_is_zero(r) || fe_cmp(r, k_p256_n) >= 0 ||
        fe_is_zero(s) || fe_cmp(s, k_p256_n) >= 0) {
        return PSA_ERROR_INVALID_SIGNATURE;
    }

    /* e = leftmost min(hash_len, 32) bytes, big-endian */
    fe e;
    if (hash_len >= 32) {
        fe_from_be(e, hash);
    } else {
        /* e = e*256 + hash[i], one byte at a time; the top word cannot
         * overflow because e < 2^248 for hash_len < 32. */
        fe_zero(e);
        for (size_t i = 0; i < hash_len; i++) {
            uint32_t carry = hash[i];
            for (int j = 0; j < 8; j++) {
                uint32_t next = e[j] >> 24;
                e[j] = (e[j] << 8) | carry;
                carry = next;
            }
        }
    }

    fe w, u1, u2;
    inv_mod(w, s, k_p256_n);
    mul_mod(u1, e, w, k_p256_n);
    mul_mod(u2, r, w, k_p256_n);

    fe tab_x[8], tab_y[8];
    build_q_table(tab_x, tab_y, qx, qy);

    int8_t dg[257], dq[257];
    int lg = sc_wnaf(dg, u1, 6); /* G: fixed-base table, 16 entries */
    int lq = sc_wnaf(dq, u2, 5); /* Q: runtime table, 8 entries */
    int len = lg > lq ? lg : lq;

    pt acc;
    pt_set_identity(&acc);
    for (int i = len - 1; i >= 0; i--) {
        pt_dbl(&acc, &acc);
        int8_t d = dg[i];
        if (d != 0) {
            int neg = d < 0;
            int idx = ((neg ? -d : d) - 1) / 2;
            fe ty;
            if (neg) {
                fe_sub(ty, k_p256_p, k_p256_g_odd[idx][1]); /* -y */
            } else {
                memcpy(ty, k_p256_g_odd[idx][1], sizeof(fe));
            }
            pt_madd(&acc, &acc, k_p256_g_odd[idx][0], ty);
        }
        d = dq[i];
        if (d != 0) {
            int neg = d < 0;
            int idx = ((neg ? -d : d) - 1) / 2;
            fe ty;
            if (neg) {
                fe_sub(ty, k_p256_p, tab_y[idx]);
            } else {
                memcpy(ty, tab_y[idx], sizeof(fe));
            }
            pt_madd(&acc, &acc, tab_x[idx], ty);
        }
    }

    if (pt_is_identity(&acc)) {
        return PSA_ERROR_INVALID_SIGNATURE; /* R = O */
    }

    /* projective final comparison: X == r*Z^2 (mod p) */
    fe z2, rhs;
    fe_sq(z2, acc.z);
    fe_mul(rhs, r, z2);
    if (fe_cmp(acc.x, rhs) == 0) {
        return PSA_SUCCESS;
    }
    /* wrap-around: valid if x_R = r + n and r + n < p */
    fe rn;
    if (sc_add_raw(rn, r, k_p256_n) == 0 && fe_cmp(rn, k_p256_p) < 0) {
        fe_mul(rhs, rn, z2);
        if (fe_cmp(acc.x, rhs) == 0) {
            return PSA_SUCCESS;
        }
    }
    return PSA_ERROR_INVALID_SIGNATURE;
}
