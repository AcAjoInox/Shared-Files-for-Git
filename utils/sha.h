/*
 * sha.h - SHA-1 e SHA-256 (FIPS 180-4), per gli oggetti che git shared
 * scrive da se' (D-116: il tree sintetico del percorso veloce). Il nome di
 * un oggetto git e' l'hash di "<tipo> <dimensione>\0<contenuto>".
 */
#ifndef GS_SHA_H
#define GS_SHA_H

#include <stddef.h>
#include <stdint.h>

#define SHA1_LEN   20U
#define SHA256_LEN 32U

typedef struct {
    uint32_t h[8];   /* 5 per SHA-1, 8 per SHA-256 */
    uint64_t total;  /* byte ricevuti */
    uint8_t buf[64];
    size_t n;        /* byte in buf */
    unsigned len;    /* SHA1_LEN o SHA256_LEN */
} Sha_t;

/* len = SHA1_LEN o SHA256_LEN. */
void sha_init(Sha_t *c, unsigned len);
void sha_update(Sha_t *c, const void *data, size_t n);
/* out: c->len byte. */
void sha_final(Sha_t *c, uint8_t *out);

#endif /* GS_SHA_H */
