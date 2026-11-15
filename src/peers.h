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
#include "str.h"

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

/* Scrive `self` tra i peer di ogni peer (senza duplicati). */
void peers_announce(const char *id, const Str_t *peers, const char *self,
                    PeerTally_t *t);

#endif /* GS_PEERS_H */
