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
    Str_t old_paths;   /* da .gitshared: path di prima delle rinomine (mv,
                        * D-101), elenco compatto */
    Str_t branches;    /* branch attivi (o default), elenco compatto */
    Str_t peers;       /* radici dei peer, elenco compatto */
    Str_t pending_src; /* link in attesa: radice della repo sorgente */
    Str_t pending_branch; /* attesa: branch del comando ("" = HEAD staccata, D-077) */
    Str_t pending_path;   /* attesa: path del file, solo per i messaggi (D-077) */
    Str_t pending_commit; /* unlink in attesa: HEAD al comando (D-081) */
    Str_t seen_url;    /* authoritySeen: URL su cui e' stata vista
                        * refs/shared-authority/<id> ("" = mai, D-112) */
    unsigned long seen_at; /* authoritySeen: quando, secondi dal 1970 */
    char id[GS_ID_HEX_LEN + 1U]; /* "" se in .gitshared non e' valido */
    unsigned in_gitshared : 1U; /* sezione in .gitshared (working tree) */
    unsigned in_config : 1U;    /* sezione [shared "<id>"] in .git/config */
    unsigned valid : 1U;        /* id e path ben formati */
    unsigned pending : 2U;      /* ePending_t */
    unsigned pending_remote_key : 1U; /* chiave pendingRemote presente */
    unsigned pending_remote : 1U; /* link remoto in attesa: pending_src e'
                                   * un URL, nessuna membership (D-106) */
} SharedEntry_t;

typedef struct {
    Str_t top;    /* radice del working tree */
    Str_t prefix; /* cartella corrente relativa a top: "" oppure "dir/" */
    Str_t branch; /* branch corrente senza "refs/heads/"; "" se HEAD staccata */
    Str_t objfmt; /* algoritmo degli oid: "sha1" o "sha256" (D-033) */
    Str_t self_path; /* shared.selfPath: dove la repo ricorda di essere */
    SharedEntry_t *v;
    size_t n;
    size_t cap;
} Repo_t;

#define REPO_INIT { STR_INIT, STR_INIT, STR_INIT, STR_INIT, STR_INIT, NULL, 0U, 0U }

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

/* Nomi brevi dei branch locali, in un elenco compatto. */
eGsErr_t repo_branch_names(Str_t *out);

/* Branch locali letti al primo bisogno: un processo solo se serve. */
typedef struct {
    Str_t names;
    unsigned loaded : 1U;
} BranchNames_t;

#define BRANCHNAMES_INIT { STR_INIT, 0U }

/*
 * 1 se nessuno dei branch attivi della voce esiste: il file e' sospeso
 * ovunque, di solito dopo una rinomina (reportIdea B-21, D-085). In
 * `list` i branch attivi separati da ", ". Un errore nel leggere i branch
 * vale 0: meglio nessun avviso che uno falso.
 */
int shared_branches_gone(BranchNames_t *bn, const SharedEntry_t *e, Str_t *list);
void branchnames_free(BranchNames_t *bn);

/* 1 se s e' un ID valido: 32 cifre esadecimali minuscole (D-006). */
int shared_id_is_valid(const char *s);

/* Nuovo ID casuale: UUID v4 in 32 cifre esadecimali minuscole (D-006). */
eGsErr_t shared_new_id(char id[GS_ID_HEX_LEN + 1U]);

/*
 * Completa o scarta le operazioni in attesa (REQ-F-023, D-077). Conta il
 * branch su cui e' stato lanciato il comando, non quello corrente:
 *   - sezione committata su quel branch -> si completa, da qualunque branch;
 *   - sezione assente, si e' su quel branch e manca anche nel working tree:
 *     se un altro branch locale la dichiara si completa da li' (D-079),
 *     altrimenti e' annullata dall'utente (git restore) e si scarta;
 *   - altrimenti l'attesa resta.
 * Senza branch registrato (HEAD staccata al comando) vale HEAD.
 *
 * unlink (D-081): si completa quando HEAD o un branch locale discende dal
 * commit del comando e il suo .gitshared non dichiara piu' l'ID; si
 * scarta sul branch del comando se la sezione e' tornata nel working tree.
 *
 * may_discard = 0: si completa soltanto, nessuna attesa si scarta. E' il
 * caso di un completamento chiesto da un'altra repo (link verso una
 * sorgente in attesa, D-100): scartare lo decide solo la repo stessa.
 */
eGsErr_t repo_complete_pending(Repo_t *r, GitBatch_t *b, int may_discard);

/* 1 se almeno una voce ha un set, link o unlink in attesa. */
int repo_has_pending(const Repo_t *r);

/* *declared = 1 se il .gitshared committato in <rev> dichiara l'ID. */
eGsErr_t repo_rev_declares(GitBatch_t *b, const char *rev, const char *id,
                           int *declared);

/* Come repo_rev_declares; se lo dichiara, in path il path del file in
 * quel .gitshared (path puo' essere NULL). */
eGsErr_t repo_rev_path(GitBatch_t *b, const char *rev, const char *id,
                       Str_t *path, int *declared);

/* Un branch locale il cui .gitshared committato dichiara l'ID (D-079):
 * *found = 1 e il nome in branch (senza "refs/heads/"); in path, se non
 * e' NULL, il path del file su quel branch. */
eGsErr_t repo_declaring_branch(const char *id, GitBatch_t *b, Str_t *branch,
                               Str_t *path, int *found);

/*
 * Toglie i dati locali di un file shared (REQ-F-003): i peer dimenticano
 * questa repo, refs/shared/<id> (e le ref di lavoro) e la sezione
 * [shared "<id>"] spariscono. Un peer irraggiungibile e' un avviso.
 */
eGsErr_t repo_forget(const Repo_t *r, SharedEntry_t *e, GitBatch_t *b);

/* Scrive shared.selfPath (REQ-F-017). */
eGsErr_t repo_set_self(Repo_t *r, const char *path);

/*
 * REQ-F-017, D-084: con almeno un file collegato, la repo ricorda dove
 * sta. Se non lo ricorda ancora lo scrive; se ricorda un altro path avvisa
 * di lanciare relocate. Non in un worktree secondario (.git e' un file):
 * config e ref sono quelli della repo principale.
 */
void repo_check_self(Repo_t *r);

#endif /* GS_SHARED_H */
