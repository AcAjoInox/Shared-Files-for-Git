/*
 * state.h - stato di un file shared, ricavato ogni volta (D-041, D-054,
 * D-065, D-076). Nessuno stato salvato: solo H (blob in HEAD), I (blob
 * nell'index), T (blob dell'ultima versione), la catena e la storia del
 * file sul branch.
 */
#ifndef GS_STATE_H
#define GS_STATE_H

#include "err.h"
#include "git.h"
#include "shared.h"

typedef enum {
    STATE_ALIGNED = 0, /* H = T */
    STATE_TO_APPLY,    /* base piu' vecchia di T, nessuna modifica locale */
    STATE_TO_PUBLISH,  /* base = T, H diverso: modifica non pubblicata */
    STATE_DIVERGED,    /* modifica da una base vecchia, o nessuna base */
    STATE_TO_COMMIT,   /* H diverso da T, index = T: update da committare */
    STATE_PENDING,     /* set/link/unlink non ancora committato */
    STATE_UNLINKED,    /* in .gitshared ma non in .git/config (es. clone) */
    STATE_INCONSISTENT /* in config ma non in .gitshared, o file assente */
} eState_t;

/* 1 per gli stati che dipendono dal branch (possono essere "sospesi"). */
int state_is_derived(eState_t st);

/* Nome dello stato per l'utente. */
const char *state_name(eState_t st);

eGsErr_t state_derive(const Repo_t *r, const SharedEntry_t *e, GitBatch_t *b,
                      eState_t *st);

#endif /* GS_STATE_H */
