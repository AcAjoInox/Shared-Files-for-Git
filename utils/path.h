/*
 * path.h - operazioni sui path come stringhe. '/' e '\' sono entrambi
 * separatori in ingresso; in uscita si usa sempre '/'.
 */
#ifndef GS_PATH_H
#define GS_PATH_H

#include "str.h"

/* 1 se il path e' assoluto: inizia con un separatore o con "X:". */
int path_is_absolute(const char *p);

/* Cartella ("." se non c'e') e nome dell'ultimo componente. */
void path_split(const char *p, Str_t *dir, const char **name);

/*
 * Unisce base e rel e risolve "." e "..". Ritorna 0 se il risultato e'
 * vuoto o esce sopra la base: "a/" + "../../x" non e' valido.
 */
int path_join_norm(const char *base, const char *rel, Str_t *out);

#endif /* GS_PATH_H */
