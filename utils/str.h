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
void str_setlen(Str_t *s, size_t len); /* accorcia: len <= s->len */
const char *str_cstr(const Str_t *s);
int str_eq(const Str_t *s, const char *p);

/*
 * Testo scritto da altri (S-07): lunghezza in byte del carattere in testa
 * a p se si puo' stampare senza effetti sul terminale, 0 altrimenti.
 * Stampabili: ASCII da 0x20 a 0x7E e le sequenze UTF-8 valide (forma piu'
 * corta) che non sono controlli C1 (U+0080...U+009F), controlli bidi o
 * caratteri invisibili (U+061C, U+200B...U+200F, U+2028...U+202E,
 * U+2066...U+2069, U+FEFF) ne' surrogati. Il byte nullo da' 0.
 */
size_t str_char_printable(const char *p);

/* Aggiunge p a s con ogni carattere non stampabile (str_char_printable)
 * sostituito da '?', un byte alla volta. */
void str_add_printable(Str_t *s, const char *p);

/*
 * Elenchi compatti: piu' stringhe in un solo buffer, ognuna chiusa da
 * '\0' ("main\0release\0"). Un'allocazione per tutto l'elenco.
 *   for (p = str_znext(s, NULL); p != NULL; p = str_znext(s, p)) ...
 */
void str_addz(Str_t *s, const char *p);
const char *str_znext(const Str_t *s, const char *cur);
int str_zhas(const Str_t *s, const char *p);
size_t str_zcount(const Str_t *s);

#endif /* GS_STR_H */
