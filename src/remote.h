/*
 * remote.h - la catena in remoto: il creatore (D-103...D-107, D-110).
 *
 * In remoto non ci sono peer: ogni file ha un creatore, la repo remota
 * verso cui e' partito il primo "git shared push <remote>". Il suo URL sta
 * nella catena (voce "creator", chain.h) e viaggia con le versioni. Il
 * creatore e' l'arbitro della versione ufficiale: il push di git aggiorna
 * la sua ref solo in fast-forward.
 *
 * La rete solo su richiesta (D-110): push e pull, status --remote, link
 * <url> <id>. L'ultima versione vista del creatore resta in
 * refs/shared-creator/<id>, come refs/remotes/: i comandi locali la usano
 * come la versione di un peer.
 *
 * Il trasporto e' quello di git (push, fetch, credenziali, permessi):
 * nessun server nostro. Nessun dato locale va in remoto: solo la catena.
 */
#ifndef GS_REMOTE_H
#define GS_REMOTE_H

#include <stdint.h>

#include "err.h"
#include "git.h"
#include "str.h"

/* Esito di un push della catena verso una repo remota. */
typedef enum {
    REMOTE_PUSH_OK = 0,   /* accettato, o la repo aveva gia' la versione */
    REMOTE_PUSH_NEWER,    /* rifiutato: la repo ha una versione piu' nuova
                           * o divergente ([rejected]: non-fast-forward,
                           * fetch first, stale info) */
    REMOTE_PUSH_REFUSED   /* rifiutato dal server ([remote rejected]:
                           * permessi, hook) o repo irraggiungibile */
} eRemotePush_t;

/* Esito della lettura di una catena dal creatore. */
typedef enum {
    REMOTE_GOT = 0,       /* refs/shared-creator/<id> aggiornata */
    REMOTE_MISSING,       /* il creatore non ha refs/shared/<id> */
    REMOTE_UNREACHABLE    /* repo irraggiungibile o fetch fallito */
} eRemoteFetch_t;

/* URL del remote `name` della repo corrente (git remote get-url).
 * GS_ERR_GIT se la repo non ha quel remote. */
eGsErr_t remote_url(const char *name, Str_t *url);

/*
 * URL del creatore della versione `version` (voce "creator" del suo tree):
 * url vuoto se non c'e'. Nessun processo se la versione non ha la voce; un
 * processo (cat-file) per leggerla.
 */
eGsErr_t remote_creator_url(GitBatch_t *b, const Oid_t *version, Str_t *url);

/* Scrive nella repo il blob della voce "creator": l'URL e un fine riga. */
eGsErr_t remote_url_blob(const char *url, Oid_t *blob);

/*
 * Catene degli ID dell'elenco compatto `ids` dalla repo remota, in
 * refs/shared-creator/<id> (D-110): un fetch per tutte, con i refspec
 * sullo stdin. Se fallisce, ls-remote dice quali mancano e il fetch si
 * ripete per le altre. res[k]: esito dell'ID k.
 */
void remote_fetch_seen(const char *url, const Str_t *ids, uint8_t *res);

/*
 * Push delle versioni v[k] in refs/shared/<id k> della repo remota, senza
 * forzare: il fast-forward lo verifica git. Un processo ogni
 * REMOTE_PUSH_CHUNK ref (la riga di comando ha un limite). res[k]: esito.
 */
#define REMOTE_PUSH_CHUNK 64U
void remote_push_many(const char *url, const Str_t *ids, const Oid_t *v,
                      uint8_t *res);

/* Un solo push: come remote_push_many con un ID. */
eRemotePush_t remote_push(const char *url, const char *id, const Oid_t *v);

/* Fetch di refs/shared/<id> dalla repo remota nella ref locale `ref`
 * (link <url> <id>, D-106). */
eGsErr_t remote_fetch(const char *url, const char *id, const char *ref);

/* Ultima versione vista del creatore: refs/shared-creator/<id>. */
void remote_seen_ref(Str_t *ref, const char *id);

#endif /* GS_REMOTE_H */
