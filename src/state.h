/*
 * state.h - stato di un file shared, ricavato ogni volta (D-041, D-054,
 * D-065, D-076). Nessuno stato salvato, salvo l'unione in corso (D-078): solo H (blob in HEAD), I (blob
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
    STATE_MERGING,     /* unione in corso: conflitto o risoluzione in staging */
    STATE_PENDING,     /* set/link/unlink non ancora committato */
    STATE_UNLINKED,    /* in .gitshared ma non in .git/config (es. clone) */
    STATE_INCONSISTENT, /* in config ma non in .gitshared, o file assente */
    STATE_ABSENT       /* in config, non dichiarato su questo branch ma su un
                        * altro branch locale: qui non e' shared (B-16) */
} eState_t;

/* 1 per gli stati che dipendono dal branch (possono essere "sospesi"). */
int state_is_derived(eState_t st);

/* Nome dello stato per l'utente. */
const char *state_name(eState_t st);

/* Parola chiave fissa dello stato per status --porcelain (D-119): non si
 * traduce e non cambia. */
const char *state_keyword(eState_t st);

/* Cio' che lo stato ha usato: chi agisce dopo (publish, update) parte da
 * questi valori invece di rileggerli (reportIdea B-31). Un oid con
 * len = 0 non e' stato ricavato. */
typedef struct {
    Oid_t h;    /* blob del file in HEAD */
    Oid_t t;    /* blob dell'ultima versione */
    Oid_t tip;  /* commit dell'ultima versione (refs/shared/<id>) */
    Oid_t base; /* blob della base; solo per "divergente", 0 = nessuna base */
} StateInfo_t;

/* info puo' essere NULL. */
eGsErr_t state_derive(const Repo_t *r, const SharedEntry_t *e, GitBatch_t *b,
                      eState_t *st, StateInfo_t *info);

/*
 * Base nella storia del file (path e old_paths, che puo' essere NULL) tra
 * le versioni di chain (v[0] = T, indici crescenti = piu' vecchie):
 * *found = 0 se nessuna versione compare; altrimenti *base e' il suo
 * indice in chain. Serve anche a link su una destinazione che esiste
 * (D-062), con la catena appena ricevuta.
 */
eGsErr_t state_find_base(const Repo_t *r, const char *path,
                         const Str_t *old_paths, const OidList_t *chain,
                         size_t *base, int *found);

/*
 * Rinomina con git shared mv non ancora committata (D-101): il file non e'
 * in HEAD al path di .gitshared, ma il .gitshared di HEAD lo dichiara a un
 * altro path, che in HEAD c'e'. *found = 1 e, se from non e' NULL, quel
 * path. Un processo (il .gitshared di HEAD), solo quando serve.
 */
eGsErr_t state_renamed(GitBatch_t *b, const SharedEntry_t *e, Str_t *from,
                       int *found);

#endif /* GS_STATE_H */
