/*
 * shared.h - modello: la repo corrente e i suoi file shared (§6.3).
 *
 * Una voce unisce due fonti: la sezione [shared "<nome>"] di .gitshared
 * (versionata: nome, id, path) e la sezione [shared "<id>"] di .git/config
 * (locale: branch attivi, peer, operazione in attesa; D-068).
 */
#ifndef GS_SHARED_H
#define GS_SHARED_H

#include <stddef.h>

#include "bounds.h"
#include "err.h"
#include "git.h"
#include "str.h"

/* Operazione in attesa di commit (REQ-F-023). */
typedef enum {
    SHARED_PENDING_NONE = 0,
    SHARED_PENDING_SET,
    SHARED_PENDING_LINK,
    SHARED_PENDING_UNLINK
} ePending_t;

typedef struct {
    Str_t name;        /* nome della sezione in .gitshared */
    Str_t path;        /* da .gitshared: relativo alla radice, separatore '/' */
    Str_t branches;    /* branch attivi (o default), elenco compatto */
    Str_t peers;       /* radici dei peer, elenco compatto */
    Str_t pending_src; /* link in attesa: radice della repo sorgente */
    char id[GS_ID_HEX_LEN + 1U]; /* "" se in .gitshared non e' valido */
    unsigned in_gitshared : 1U; /* sezione in .gitshared (working tree) */
    unsigned in_config : 1U;    /* sezione [shared "<id>"] in .git/config */
    unsigned valid : 1U;        /* id e path ben formati */
    unsigned pending : 2U;      /* ePending_t */
} SharedEntry_t;

typedef struct {
    Str_t top;    /* radice del working tree */
    Str_t prefix; /* cartella corrente relativa a top: "" oppure "dir/" */
    Str_t branch; /* branch corrente senza "refs/heads/"; "" se HEAD staccata */
    Str_t objfmt; /* algoritmo degli oid: "sha1" o "sha256" (D-033) */
    SharedEntry_t *v;
    size_t n;
    size_t cap;
    unsigned auto_publish : 1U; /* shared.autoPublish, default 0 (D-073) */
} Repo_t;

#define REPO_INIT { STR_INIT, STR_INIT, STR_INIT, STR_INIT, NULL, 0U, 0U, 0U }

/*
 * Apre la repo della cartella corrente e carica i file shared.
 * GS_ERR_NOT_REPO fuori da un working tree; GS_ERR_FORMAT se .gitshared
 * non e' un config valido (git ha gia' indicato la riga sbagliata).
 */
eGsErr_t repo_open(Repo_t *r);
void repo_close(Repo_t *r);

/*
 * Radice, prefisso e algoritmo degli oid della repo che contiene `dir`,
 * con un solo processo. GS_ERR_NOT_REPO se `dir` non e' in una repo.
 */
eGsErr_t repo_locate(const char *dir, Str_t *top, Str_t *prefix,
                     Str_t *objfmt);

/*
 * Voci di un .gitshared qualunque: dal working tree di un'altra repo
 * (src = file) o da un commit (src = "<rev>:.gitshared", is_blob = 1).
 * with_branches: carica anche i branch di default in `branches`.
 */
eGsErr_t repo_load_gitshared(Repo_t *r, const char *src, int is_blob,
                             int with_branches);

/* Nuova voce vuota in fondo. Invalida i puntatori alle voci precedenti. */
SharedEntry_t *repo_add(Repo_t *r);
SharedEntry_t *repo_find_path(Repo_t *r, const char *path);
SharedEntry_t *repo_find_name(Repo_t *r, const char *name);
SharedEntry_t *repo_find_id(Repo_t *r, const char *id);

/* 1 se branch e' tra i branch attivi della voce (nomi esatti nella v0). */
int shared_is_active(const SharedEntry_t *e, const char *branch);

/* Nuovo ID casuale: UUID v4 in 32 cifre esadecimali minuscole (D-006). */
eGsErr_t shared_new_id(char id[GS_ID_HEX_LEN + 1U]);

/*
 * Completa o scarta le operazioni in attesa (REQ-F-023): completa quelle
 * gia' committate (la sezione e' in HEAD:.gitshared), scarta quelle la cui
 * sezione non c'e' piu' nemmeno nel working tree. Nella v0: set e link.
 */
eGsErr_t repo_complete_pending(Repo_t *r, GitBatch_t *b);

#endif /* GS_SHARED_H */
