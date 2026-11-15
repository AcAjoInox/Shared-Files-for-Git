/*
 * git.h - accesso a git tramite i suoi comandi (D-017).
 *
 * Il programma git lanciato e' quello della exec-path che git stesso ci
 * passa in GIT_EXEC_PATH: la stessa versione che ha lanciato l'estensione.
 * Senza GIT_EXEC_PATH (eseguibile lanciato a mano) si usa "git" dal PATH.
 */
#ifndef GS_GIT_H
#define GS_GIT_H

#include <stddef.h>
#include <stdint.h>

#include "bounds.h"
#include "err.h"
#include "proc.h"
#include "str.h"

/* ---- Oid ---------------------------------------------------------------
 * In binario: 33 byte invece dei 65 della forma esadecimale. */

typedef struct {
    uint8_t len;                 /* 20 (SHA-1), 32 (SHA-256), 0 = nessuno */
    uint8_t raw[GS_OID_RAW_MAX];
} Oid_t;

#define OID_HEX_SIZE (GS_OID_HEX_MAX + 1U) /* forma esadecimale + '\0' */

/* 1 se hex[0..len) e' un oid esadecimale valido (40 o 64 cifre). */
int oid_from_hex(Oid_t *oid, const char *hex, size_t len);
void oid_to_hex(const Oid_t *oid, char hex[OID_HEX_SIZE]);
int oid_eq(const Oid_t *a, const Oid_t *b);

typedef struct {
    Oid_t *v;
    size_t n;
    size_t cap;
} OidList_t;

#define OIDLIST_INIT { NULL, 0U, 0U }

void oidlist_add(OidList_t *l, const Oid_t *oid);
int oidlist_has(const OidList_t *l, const Oid_t *oid);
void oidlist_free(OidList_t *l);

/* ---- Processi git ------------------------------------------------------ */

/* Come proc_start/proc_capture, con git davanti agli argomenti.
 * args termina con NULL. */
eGsErr_t git_start(Proc_t *p, const char *const args[], unsigned flags);
eGsErr_t git_capture(const char *const args[], unsigned flags, Str_t *out,
                     int *exit_code);

/* Esegue e pretende il codice 0 (GS_ERR_GIT altrimenti). out puo' essere
 * NULL; se c'e', riceve lo stdout senza il fine riga finale. */
eGsErr_t git_run(const char *const args[], unsigned flags, Str_t *out);

/*
 * Aggiornamento atomico e condizionato di una ref (REQ-NF-005): riesce
 * solo se la ref vale old_oid, o non esiste se old_oid e' NULL.
 */
eGsErr_t git_update_ref(const char *ref, const Oid_t *new_oid,
                        const Oid_t *old_oid, const char *reason);

/* Cancella le ref di un elenco compatto (str_addz) con un solo processo. */
eGsErr_t git_delete_refs(const Str_t *refs);

/* ---- Interrogazioni in serie -------------------------------------------
 * Un solo processo "git cat-file --batch-check" risponde a tutte le
 * domande ("HEAD:<path>", "refs/shared/<id>:content", ...): si paga
 * l'avvio di un processo una volta sola. */

typedef enum {
    GIT_OBJ_MISSING = 0, /* nome che non porta a nessun oggetto */
    GIT_OBJ_BLOB,
    GIT_OBJ_TREE,
    GIT_OBJ_COMMIT,
    GIT_OBJ_OTHER,
    GIT_OBJ_ERROR        /* dialogo con git interrotto */
} eGitObj_t;

typedef struct {
    Proc_t p;
    Str_t line;
} GitBatch_t;

eGsErr_t git_batch_open(GitBatch_t *b);
eGitObj_t git_batch_query(GitBatch_t *b, const char *name, Oid_t *oid);
/* 1 se il nome porta a un blob. */
int git_batch_blob(GitBatch_t *b, const char *name, Oid_t *oid);
void git_batch_close(GitBatch_t *b);

#endif /* GS_GIT_H */
