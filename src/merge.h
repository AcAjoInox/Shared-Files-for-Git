/*
 * merge.h - unione a 3 vie di un file shared (REQ-F-011, D-078).
 *
 * L'unione si fa nell'index di git, con il solo plumbing: stage 1/2/3 del
 * path (base, nostra, loro) e "git merge-index git-merge-one-file". Il
 * conflitto e' quello di git: stage nell'index, marker nel file. Un file
 * binario (D-108) non si unisce mai: conflitto senza marker, con la
 * versione nostra nel file. Il contenuto non passa mai dai buffer
 * dell'estensione (D-042).
 *
 * Unico dato salvato: refs/shared-merge/<id>, l'equivalente di MERGE_HEAD.
 * Punta a un commit provvisorio i cui genitori sono le versioni che il
 * contenuto risolto incorpora: la T di partenza e, per una divergenza tra
 * peer, la versione del peer. Vale solo finche' il path e' in conflitto
 * nell'index o l'index e' diverso da HEAD; altrimenti e' scaduta, e la
 * toglie il primo comando che ricava lo stato (state_derive, D-092).
 */
#ifndef GS_MERGE_H
#define GS_MERGE_H

#include "err.h"
#include "git.h"
#include "str.h"

/* "refs/shared-merge/<id>" in ref (che viene prima svuotata). */
void merge_ref(Str_t *ref, const char *id);

/*
 * 1 se un merge registrato in refs/shared-merge/<id> e' ancora in corso:
 * il path ha stage di conflitto (*unmerged = 1), oppure la risoluzione e'
 * in staging (index diverso da h, il blob di HEAD). Il cat-file deve
 * essere stato aperto dopo l'ultima modifica dell'index.
 */
int merge_is_live(GitBatch_t *b, const char *path, const Oid_t *h,
                  int *unmerged);

/* Blob vuoto, scritto nella repo: base di un'unione senza base (D-029). */
eGsErr_t merge_empty_blob(Oid_t *out);

/* Blob del path nell'index (stage 0), letto con un processo nuovo. */
eGsErr_t merge_index_blob(const char *top, const char *path, Oid_t *out);

/* Esito di merge_three. */
#define MERGE_CLEAN    0 /* risultato nell'index (stage 0) e nel file */
#define MERGE_CONFLICT 1 /* testo: stage 1/2/3, marker nel file */
#define MERGE_BINARY   2 /* binario: stage 1/2/3, versione nostra nel file */

/*
 * Unisce ours e theirs con base base sul path dato (relativo a top);
 * *conflict = MERGE_CLEAN, MERGE_CONFLICT o MERGE_BINARY. Binario (D-108):
 * una delle tre versioni ha un byte nullo nei primi 8000 byte, la regola
 * di git; git-merge-one-file non lo unisce mai, e il file resta la
 * versione nostra, senza marker. Il working tree viene sovrascritto.
 */
eGsErr_t merge_three(const char *top, const char *path, const Oid_t *base,
                     const Oid_t *ours, const Oid_t *theirs, int *conflict);

/* Toglie refs/shared-merge/<id>, che il chiamante ha visto esistere. */
eGsErr_t merge_drop(const char *id);

/* 1 se il commit a e' antenato di b (o uguale): un processo. */
int merge_is_ancestor(const Oid_t *a, const Oid_t *b);

/* Antenato comune di due versioni; out->len = 0 se non c'e'. */
eGsErr_t merge_base(const Oid_t *a, const Oid_t *b, Oid_t *out);

#endif /* GS_MERGE_H */
