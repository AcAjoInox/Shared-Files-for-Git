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
 * Commit di versione con il blob dato e i genitori dati: parent = NULL per
 * la prima versione; parent2 != NULL per l'unione di due versioni (D-078).
 * Autore e data dell'utente (D-013), messaggio "shared: <name>". Non tocca
 * nessuna ref.
 */
eGsErr_t chain_commit(const Oid_t *blob, const Oid_t *parent,
                      const Oid_t *parent2, const char *name, Oid_t *out);

/* Prima versione: chain_commit + ref creata solo se non esiste. */
eGsErr_t chain_create_first(const char *id, const Oid_t *blob,
                            const char *name);

/*
 * Blob di tutte le versioni raggiungibili da T, con entrambi i genitori
 * delle unioni (reportIdea B-46): out->v[0] = T, e ogni versione viene prima
 * dei suoi genitori (indici crescenti = piu' vecchie). Tra versioni nessuna
 * delle quali discende dall'altra l'ordine e' quello della visita: la linea
 * dei primi genitori prima di quella dei secondi. Tre domande per versione
 * sullo stesso processo cat-file: nessun processo nuovo. Catena assente:
 * lista vuota.
 */
eGsErr_t chain_blobs(GitBatch_t *b, const char *id, OidList_t *out);

/* Come chain_blobs, a partire da una ref qualunque (es. la ref temporanea
 * del fetch di link, D-062). */
eGsErr_t chain_blobs_ref(GitBatch_t *b, const char *ref, OidList_t *out);

#endif /* GS_CHAIN_H */
