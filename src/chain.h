/*
 * chain.h - la catena delle versioni refs/shared/<id> (D-013, D-042).
 *
 * Ogni versione e' un commit il cui tree ha una sola voce, a nome fisso:
 * "100644 blob <oid>	content" (§6.3).
 */
#ifndef GS_CHAIN_H
#define GS_CHAIN_H

#include "err.h"
#include "git.h"
#include "str.h"

/* "refs/shared/<id>" in ref (che viene prima svuotata). */
void chain_ref(Str_t *ref, const char *id);

/*
 * Commit di versione con il blob dato e il genitore dato (NULL per la
 * prima versione). Autore e data dell'utente (D-013), messaggio
 * "shared: <name>". Non tocca nessuna ref.
 */
eGsErr_t chain_commit(const Oid_t *blob, const Oid_t *parent,
                      const char *name, Oid_t *out);

/* Prima versione: chain_commit + ref creata solo se non esiste. */
eGsErr_t chain_create_first(const char *id, const Oid_t *blob,
                            const char *name);

/*
 * Blob delle versioni, dalla piu' recente (out->v[0] = T) alla piu'
 * vecchia, seguendo il primo genitore. Due domande per versione sullo
 * stesso processo cat-file: nessun processo nuovo. Catena assente: lista
 * vuota.
 */
eGsErr_t chain_blobs(GitBatch_t *b, const char *id, OidList_t *out);

#endif /* GS_CHAIN_H */
