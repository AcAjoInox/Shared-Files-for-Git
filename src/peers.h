/*
 * peers.h - le altre repo che usano un file shared (D-012, D-040).
 *
 * I peer sono radici di repo (path assoluti) in un elenco compatto
 * (str_addz). Le operazioni verso piu' peer partono tutte insieme e si
 * raccolgono dopo: il tempo totale e' quello del peer piu' lento, non la
 * somma (REQ-NF-007: 32 peer in meno di 1 s).
 *
 * Verso un peer si usa sempre --git-dir=<peer>/.git: se il peer non c'e'
 * piu', git non risale a una repo che contiene quella cartella.
 */
#ifndef GS_PEERS_H
#define GS_PEERS_H

#include <stdint.h>

#include "err.h"
#include "git.h"
#include "str.h"

/* Che cosa c'e' al path di un peer (prune, REQ-F-014). */
typedef enum {
    PEER_MEMBER = 0,  /* repo con la sezione [shared "<id>"] */
    PEER_LEFT,        /* repo che non condivide piu' il file */
    PEER_GONE,        /* nessuna repo a quel path */
    PEER_UNREACHABLE, /* la repo c'e' ma git non la legge (lock, permessi) */
    PEER_ONE_WAY      /* membro che non elenca questa repo tra i suoi peer
                       * (prune --one-way, D-118): non lo da' peers_check */
} ePeerState_t;

typedef struct {
    unsigned ok;
    unsigned rejected;    /* push rifiutato: il peer ha una versione nuova */
    unsigned unreachable; /* path inesistente, permessi, lock (REQ-F-008) */
} PeerTally_t;

/* Peer che la repo `repo` conosce per l'ID dato. GS_ERR_GIT se la repo
 * non e' raggiungibile. */
eGsErr_t peers_of(const char *repo, const char *id, Str_t *out);

/* Push di refs/shared/<id> a tutti i peer, senza forzare (D-040).
 * `label` identifica il file nei messaggi. */
void peers_push(const char *label, const char *id, const Str_t *peers,
                PeerTally_t *t);

/*
 * Fetch di refs/shared/<id> da tutti i peer in refs/shared-incoming/<id>/<k>
 * (k = posizione del peer). ok[k] = 1 se il fetch dal peer k e' riuscito.
 */
void peers_fetch(const char *label, const char *id, const Str_t *peers,
                 uint8_t *ok, PeerTally_t *t);

/* Ref temporanea del peer k: refs/shared-incoming/<id>/<k>. */
void peers_incoming_ref(Str_t *ref, const char *id, unsigned k);

/*
 * Ref temporanea di questo processo: refs/shared-incoming/<id>/t<8 cifre
 * esadecimali casuali, le stesse per tutto il processo>. Per crown e push
 * (B-SA3): con l'indice 0, due comandi sullo stesso ID in parallelo si
 * cancellerebbero la ref a vicenda, e quella di un link in attesa. prune
 * la toglie se resta.
 */
void peers_tmp_ref(Str_t *ref, const char *id);

/* Scrive `self` tra i peer di ogni peer (senza duplicati). label: il
 * file, nei messaggi (C-14). */
void peers_announce(const char *label, const char *id, const Str_t *peers,
                    const char *self, PeerTally_t *t);

/* Toglie `self` dai peer di ogni peer (unlink, REQ-F-003). Un peer che
 * non lo conosceva gia' piu' non e' un errore. */
void peers_forget(const char *label, const char *id, const Str_t *peers,
                  const char *self, PeerTally_t *t);

/* Stato di ogni peer (state[k], ePeerState_t), in parallelo. "Sparito"
 * solo se al path non c'e' piu' una cartella .git: un errore di git su
 * una repo che c'e' vale "irraggiungibile" (reportIdea B-07). */
void peers_check(const char *id, const Str_t *peers, uint8_t *state);

/* Ultima versione (refs/shared/<id>) di ogni peer, in parallelo e senza
 * fetch: tips[k].len = 0 se il peer non ha la ref o non si legge. */
void peers_tips(const char *id, const Str_t *peers, Oid_t *tips);

/* 1 se <repo>/.git esiste (cartella, o file di un worktree). */
int peers_repo_exists(const char *repo);

/* Espressione regolare (ERE) che riconosce esattamente a oppure b. */
void peers_value_regex(Str_t *out, const char *a, const char *b);

/* In ogni peer: la voce `old` (o `new`, se c'e' gia') diventa una sola
 * voce `new`; se nessuna delle due c'e', `new` si aggiunge (relocate). */
void peers_replace(const char *label, const char *id, const Str_t *peers,
                   const char *old, const char *new_path, PeerTally_t *t);

#endif /* GS_PEERS_H */
