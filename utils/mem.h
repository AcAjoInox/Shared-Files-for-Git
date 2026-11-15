/*
 * mem.h - allocazione dinamica (D-066, §8.3).
 *
 * Ogni allocazione passa da qui. Se la memoria non basta il programma
 * termina con un messaggio: il resto del codice non controlla mai NULL.
 */
#ifndef GS_MEM_H
#define GS_MEM_H

#include <stddef.h>

void *xmalloc(size_t size);
void *xrealloc(void *ptr, size_t size);
char *xstrdup(const char *s);

#endif /* GS_MEM_H */
