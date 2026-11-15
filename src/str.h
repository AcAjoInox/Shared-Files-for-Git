/*
 * str.h - stringhe a lunghezza variabile (sul modello di strbuf di git).
 *
 * Il buffer e' sempre terminato da '\0' dopo la prima scrittura.
 * Una Str_t inizializzata con STR_INIT e mai scritta ha buf == NULL:
 * per leggerla si usa str_cstr(), che restituisce "".
 */
#ifndef GS_STR_H
#define GS_STR_H

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

/* Controllo del formato printf da parte del compilatore. */
#if defined(__MINGW_PRINTF_FORMAT)
#define GS_PRINTF(f, a) __attribute__((format(__MINGW_PRINTF_FORMAT, f, a)))
#elif defined(__GNUC__)
#define GS_PRINTF(f, a) __attribute__((format(printf, f, a)))
#else
#define GS_PRINTF(f, a)
#endif

typedef struct {
    char *buf;
    size_t len; /* caratteri, '\0' escluso */
    size_t cap; /* byte allocati */
} Str_t;

#define STR_INIT { NULL, 0U, 0U }

void str_init(Str_t *s);
void str_free(Str_t *s);
void str_reset(Str_t *s);
void str_addn(Str_t *s, const char *p, size_t n);
void str_add(Str_t *s, const char *p);
void str_addch(Str_t *s, char c);
void str_addf(Str_t *s, const char *fmt, ...) GS_PRINTF(2, 3);
void str_vaddf(Str_t *s, const char *fmt, va_list ap);
void str_trim_eol(Str_t *s);
const char *str_cstr(const Str_t *s);
int str_eq(const Str_t *s, const char *p);

#endif /* GS_STR_H */
