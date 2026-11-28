/*
 * str.c - stringhe a lunghezza variabile.
 */
#include "str.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "mem.h"

void str_init(Str_t *s)
{
    s->buf = NULL;
    s->len = 0U;
    s->cap = 0U;
}

void str_free(Str_t *s)
{
    free(s->buf);
    str_init(s);
}

void str_reset(Str_t *s)
{
    s->len = 0U;
    if (s->buf != NULL) {
        s->buf[0] = '\0';
    }
}

/* Garantisce spazio per altri `extra` caratteri piu' il terminatore. */
static void str_grow(Str_t *s, size_t extra)
{
    size_t need = s->len + extra + 1U;

    if (need > s->cap) {
        size_t cap = (s->cap == 0U) ? 64U : s->cap;

        while (cap < need) {
            cap *= 2U;
        }
        s->buf = xrealloc(s->buf, cap);
        s->cap = cap;
    }
}

void str_addn(Str_t *s, const char *p, size_t n)
{
    str_grow(s, n);
    memcpy(s->buf + s->len, p, n);
    s->len += n;
    s->buf[s->len] = '\0';
}

void str_add(Str_t *s, const char *p)
{
    str_addn(s, p, strlen(p));
}

void str_addch(Str_t *s, char c)
{
    str_addn(s, &c, 1U);
}

void str_vaddf(Str_t *s, const char *fmt, va_list ap)
{
    va_list ap2;
    int n;

    va_copy(ap2, ap);
    n = vsnprintf(NULL, 0U, fmt, ap2);
    va_end(ap2);
    if (n > 0) {
        str_grow(s, (size_t)n);
        (void)vsnprintf(s->buf + s->len, (size_t)n + 1U, fmt, ap);
        s->len += (size_t)n;
    }
}

void str_addf(Str_t *s, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    str_vaddf(s, fmt, ap);
    va_end(ap);
}

void str_trim_eol(Str_t *s)
{
    while ((s->len > 0U) &&
           ((s->buf[s->len - 1U] == '\n') || (s->buf[s->len - 1U] == '\r'))) {
        s->len--;
        s->buf[s->len] = '\0';
    }
}

const char *str_cstr(const Str_t *s)
{
    return (s->buf != NULL) ? s->buf : "";
}

int str_eq(const Str_t *s, const char *p)
{
    return strcmp(str_cstr(s), p) == 0;
}

/* 1 se il punto di codice non si stampa: C1, bidi, invisibili, surrogati. */
static int cp_hidden(uint32_t cp)
{
    return (cp <= 0x9FUL) || (cp == 0x61CUL) ||
           ((cp >= 0x200BUL) && (cp <= 0x200FUL)) ||
           ((cp >= 0x2028UL) && (cp <= 0x202EUL)) ||
           ((cp >= 0x2066UL) && (cp <= 0x2069UL)) || (cp == 0xFEFFUL) ||
           ((cp >= 0xD800UL) && (cp <= 0xDFFFUL)) || (cp > 0x10FFFFUL);
}

size_t str_char_printable(const char *p)
{
    static const uint32_t min_cp[5] = { 0UL, 0UL, 0x80UL, 0x800UL, 0x10000UL };
    const unsigned char *u = (const unsigned char *)p;
    uint32_t cp;
    size_t n;
    size_t k;

    if (u[0] < 0x80U) {
        return ((u[0] >= 0x20U) && (u[0] != 0x7FU)) ? 1U : 0U;
    }
    if ((u[0] & 0xE0U) == 0xC0U) {
        n = 2U;
        cp = (uint32_t)(u[0] & 0x1FU);
    } else if ((u[0] & 0xF0U) == 0xE0U) {
        n = 3U;
        cp = (uint32_t)(u[0] & 0x0FU);
    } else if ((u[0] & 0xF8U) == 0xF0U) {
        n = 4U;
        cp = (uint32_t)(u[0] & 0x07U);
    } else {
        return 0U;
    }
    for (k = 1U; k < n; k++) {
        if ((u[k] & 0xC0U) != 0x80U) { /* anche il byte nullo */
            return 0U;
        }
        cp = (cp << 6U) | (uint32_t)(u[k] & 0x3FU);
    }
    return ((cp < min_cp[n]) || cp_hidden(cp)) ? 0U : n;
}

void str_add_printable(Str_t *s, const char *p)
{
    size_t n;

    while (*p != '\0') {
        n = str_char_printable(p);
        if (n == 0U) {
            str_addch(s, '?');
            p++;
        } else {
            str_addn(s, p, n);
            p += n;
        }
    }
}

void str_setlen(Str_t *s, size_t len)
{
    if ((s->buf != NULL) && (len <= s->len)) {
        s->len = len;
        s->buf[len] = '\0';
    }
}

void str_addz(Str_t *s, const char *p)
{
    str_addn(s, p, strlen(p) + 1U); /* '\0' compreso: fa da separatore */
}

const char *str_znext(const Str_t *s, const char *cur)
{
    const char *next = NULL;

    if (cur == NULL) {
        next = (s->len > 0U) ? s->buf : NULL;
    } else {
        next = cur + strlen(cur) + 1U;
        if (next >= (s->buf + s->len)) {
            next = NULL;
        }
    }
    return next;
}

int str_zhas(const Str_t *s, const char *p)
{
    const char *c = str_znext(s, NULL);
    int found = 0;

    while ((c != NULL) && (found == 0)) {
        found = (strcmp(c, p) == 0);
        c = str_znext(s, c);
    }
    return found;
}

size_t str_zcount(const Str_t *s)
{
    const char *c = str_znext(s, NULL);
    size_t n = 0U;

    while (c != NULL) {
        n++;
        c = str_znext(s, c);
    }
    return n;
}
