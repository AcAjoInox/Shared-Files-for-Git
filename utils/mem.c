/*
 * mem.c - allocazione dinamica con uscita se la memoria si esaurisce.
 */
#include "mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "err.h"

/* Messaggio fisso e in inglese: a memoria esaurita non si fa altro. */
static _Noreturn void out_of_memory(void)
{
    fputs("fatal: out of memory\n", stderr);
    exit(GS_EXIT_FATAL);
}

void *xmalloc(size_t size)
{
    /* malloc(0) puo' restituire NULL: si chiede sempre almeno 1 byte. */
    void *p = malloc((size == 0U) ? 1U : size);

    if (p == NULL) {
        out_of_memory();
    }
    return p;
}

void *xrealloc(void *ptr, size_t size)
{
    void *p = realloc(ptr, (size == 0U) ? 1U : size);

    if (p == NULL) {
        out_of_memory();
    }
    return p;
}

char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1U;
    char *p = xmalloc(n);

    memcpy(p, s, n);
    return p;
}
