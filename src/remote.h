/*
 * remote.h - la catena in remoto: il creatore (D-103...D-107).
 *
 * In remoto non ci sono peer: ogni file ha un creatore, la repo remota da
 * cui e' partito il primo publish in remoto. Il suo URL sta nella catena
 * (voce "creator", chain.h) e viaggia con le versioni. Il creatore e'
 * l'arbitro della versione ufficiale: il push di git aggiorna la sua ref
 * solo in fast-forward. Chi riceve chiede al creatore a che punto e' la
 * ref (ls-remote) e, se serve, fa il fetch della sola catena.
 *
 * Il trasporto e' quello di git (push, fetch, credenziali, permessi):
 * nessun server nostro. Nessun dato locale va in remoto: solo la catena.
 */
#ifndef GS_REMOTE_H
#define GS_REMOTE_H

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
 * Punta di refs/shared/<id> nella repo remota (ls-remote): tip->len = 0
 * se la ref non c'e'. GS_ERR_GIT se la repo non si raggiunge.
 */
eGsErr_t remote_tip(const char *url, const char *id, Oid_t *tip);

/* Fetch di refs/shared/<id> dalla repo remota nella ref locale `ref`. */
eGsErr_t remote_fetch(const char *url, const char *id, const char *ref);

/* Push della versione v in refs/shared/<id> della repo remota, senza
 * forzare: il fast-forward lo verifica git. */
eRemotePush_t remote_push(const char *url, const char *id, const Oid_t *v);

/* Ref temporanea del fetch dal creatore: refs/shared-incoming/<id>/creator. */
void remote_incoming_ref(Str_t *ref, const char *id);

#endif /* GS_REMOTE_H */
