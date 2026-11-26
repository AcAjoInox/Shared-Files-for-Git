/*
 * sha.c - SHA-1 e SHA-256 (FIPS 180-4).
 */
#include "sha.h"

#include <string.h>

static const uint32_t k256[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
    0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
    0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
    0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

static uint32_t rol(uint32_t x, unsigned n)
{
    return (x << n) | (x >> (32U - n));
}

static uint32_t ror(uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32U - n));
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24U) | ((uint32_t)p[1] << 16U) |
           ((uint32_t)p[2] << 8U) | (uint32_t)p[3];
}

static void block1(uint32_t h[8], const uint8_t *p)
{
    uint32_t w[80];
    uint32_t a = h[0];
    uint32_t b = h[1];
    uint32_t c = h[2];
    uint32_t d = h[3];
    uint32_t e = h[4];
    uint32_t f;
    uint32_t k;
    uint32_t t;
    unsigned i;

    for (i = 0U; i < 16U; i++) {
        w[i] = be32(p + (4U * i));
    }
    for (i = 16U; i < 80U; i++) {
        w[i] = rol(w[i - 3U] ^ w[i - 8U] ^ w[i - 14U] ^ w[i - 16U], 1U);
    }
    for (i = 0U; i < 80U; i++) {
        if (i < 20U) {
            f = (b & c) | (~b & d);
            k = 0x5A827999U;
        } else if (i < 40U) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1U;
        } else if (i < 60U) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCU;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6U;
        }
        t = rol(a, 5U) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol(b, 30U);
        b = a;
        a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

static void block256(uint32_t h[8], const uint8_t *p)
{
    uint32_t w[64];
    uint32_t v[8];
    uint32_t s0;
    uint32_t s1;
    uint32_t t1;
    uint32_t t2;
    unsigned i;

    for (i = 0U; i < 16U; i++) {
        w[i] = be32(p + (4U * i));
    }
    for (i = 16U; i < 64U; i++) {
        s0 = ror(w[i - 15U], 7U) ^ ror(w[i - 15U], 18U) ^ (w[i - 15U] >> 3U);
        s1 = ror(w[i - 2U], 17U) ^ ror(w[i - 2U], 19U) ^ (w[i - 2U] >> 10U);
        w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
    }
    memcpy(v, h, sizeof v);
    for (i = 0U; i < 64U; i++) {
        s1 = ror(v[4], 6U) ^ ror(v[4], 11U) ^ ror(v[4], 25U);
        t1 = v[7] + s1 + ((v[4] & v[5]) ^ (~v[4] & v[6])) + k256[i] + w[i];
        s0 = ror(v[0], 2U) ^ ror(v[0], 13U) ^ ror(v[0], 22U);
        t2 = s0 + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
        v[7] = v[6];
        v[6] = v[5];
        v[5] = v[4];
        v[4] = v[3] + t1;
        v[3] = v[2];
        v[2] = v[1];
        v[1] = v[0];
        v[0] = t1 + t2;
    }
    for (i = 0U; i < 8U; i++) {
        h[i] += v[i];
    }
}

static void block(Sha_t *c, const uint8_t *p)
{
    if (c->len == SHA1_LEN) {
        block1(c->h, p);
    } else {
        block256(c->h, p);
    }
}

void sha_init(Sha_t *c, unsigned len)
{
    static const uint32_t h1[5] = { 0x67452301U, 0xEFCDAB89U, 0x98BADCFEU,
                                    0x10325476U, 0xC3D2E1F0U };
    static const uint32_t h256[8] = { 0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U,
                                      0xa54ff53aU, 0x510e527fU, 0x9b05688cU,
                                      0x1f83d9abU, 0x5be0cd19U };

    memset(c, 0, sizeof *c);
    c->len = (len == SHA1_LEN) ? SHA1_LEN : SHA256_LEN;
    if (c->len == SHA1_LEN) {
        memcpy(c->h, h1, sizeof h1);
    } else {
        memcpy(c->h, h256, sizeof h256);
    }
}

void sha_update(Sha_t *c, const void *data, size_t n)
{
    const uint8_t *p = data;
    size_t take;

    c->total += (uint64_t)n;
    while (n > 0U) {
        take = 64U - c->n;
        if (take > n) {
            take = n;
        }
        memcpy(c->buf + c->n, p, take);
        c->n += take;
        p += take;
        n -= take;
        if (c->n == 64U) {
            block(c, c->buf);
            c->n = 0U;
        }
    }
}

void sha_final(Sha_t *c, uint8_t *out)
{
    const uint64_t bits = c->total * 8U;
    const unsigned words = c->len / 4U;
    uint8_t pad = 0x80U;
    uint8_t lenbuf[8];
    unsigned i;

    sha_update(c, &pad, 1U);
    pad = 0U;
    while (c->n != 56U) {
        sha_update(c, &pad, 1U);
    }
    for (i = 0U; i < 8U; i++) {
        lenbuf[i] = (uint8_t)(bits >> (56U - (8U * i)));
    }
    sha_update(c, lenbuf, sizeof lenbuf);
    for (i = 0U; i < words; i++) {
        out[4U * i] = (uint8_t)(c->h[i] >> 24U);
        out[(4U * i) + 1U] = (uint8_t)(c->h[i] >> 16U);
        out[(4U * i) + 2U] = (uint8_t)(c->h[i] >> 8U);
        out[(4U * i) + 3U] = (uint8_t)c->h[i];
    }
}
