/*
 * str.c - stringhe a lunghezza variabile.
 */
#include "str.h"

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
