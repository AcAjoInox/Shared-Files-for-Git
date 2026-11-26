/*
 * receive.h - ricevere le versioni dei peer (D-015, D-092, D-099).
 *
 * Ricevere = ottenere gli oggetti e avanzare refs/shared/<id> in
 * fast-forward; non tocca working tree e index. Lo fa il primo comando che
 * ricava lo stato (status, publish, update), a partire dagli avvisi
 * (inbox.h):
 *   - avviso della versione T: si cancella;
 *   - versione gia' nella repo: niente fetch;
 *   - altrimenti un fetch per repo annunciante, da tutti i peer se la repo
 *     non e' tra i peer (spostata, path scritto in un'altra forma);
 *   - T avanza sulla versione ricevuta che la contiene (D-092);
 *   - l'avviso si cancella quando T contiene la versione; resta se la
 *     versione non e' arrivata o non discende da T (da unire).
 * Senza avvisi nessun processo. update --scan fa il fetch da tutti i peer.
 *
 * In remoto (D-110): se T ha un creatore, l'ultima versione vista
 * (refs/shared-authority/<id>) entra tra le versioni ricevute come quella
 * di un peer. Nessuna rete: la ref la aggiornano push, pull e
 * status --remote (remote.h).
 *
 * Cambi di autorita' (D-111, REQ-F-030): T non avanza mai su una versione
 * che cambia l'autorita' (creatore o ultimo sovrano, auth.h) senza
 * un'accettazione esplicita, nemmeno in fast-forward. Conta l'autorita'
 * che la versione porterebbe a T: la sua se discende da T; per una
 * versione da unire, quella dell'unione a 3 vie (un peer rimasto
 * all'autorita' vecchia non cambia nulla). Il primo creatore non si
 * sceglie: un file senza creatore lo prende. Una versione con
 * un'autorita':
 *   - accettata (authorityAccepted): candidata come le altre;
 *   - rifiutata (authorityRejected): non candidata, il suo avviso si
 *     cancella;
 *   - da decidere: al suo posto vale l'ultima versione tra T (o la versione
 *     comune) e lei che non cambia l'autorita' (rev-list --ancestry-path,
 *     ordine topologico), e il cambio va in Received_t.changes.
 * authorityAccepted vale una volta: si toglie quando T ha quell'autorita',
 * o quando nessuna versione ricevuta ce l'ha piu'. authorityRejected si
 * toglie se T arriva comunque a quell'autorita'.
 */
#ifndef GS_RECEIVE_H
#define GS_RECEIVE_H

#include "auth.h"
#include "err.h"
#include "git.h"
#include "shared.h"
#include "str.h"

/* Un cambio di autorita' in attesa di decisione (REQ-F-030). */
typedef struct {
    Str_t key;       /* valore per authorityAccepted / authorityRejected */
    Str_t authority; /* URL della nuova autorita' */
    Str_t from;      /* repo o URL da cui e' arrivata la versione */
    Oid_t version;   /* la versione ricevuta che cambia l'autorita' */
    unsigned creator : 1U;      /* cambia anche il creatore */
    unsigned by_authority : 1U; /* vista sull'autorita' stessa: passaggio di
                                 * consegne, oppure dirottamento */
    unsigned rejected : 1U;     /* gia' rifiutato: le sue versioni non sono
                                 * candidate, ma crown --accept lo riprende */
} AuthChange_t;

typedef struct {
    OidList_t heads; /* versioni che non discendono da T e da cui T non
                      * discende: da unire (REQ-F-011) */
    Str_t missing;   /* repo che hanno annunciato una versione non arrivata
                      * (elenco compatto, senza doppioni) */
    Str_t strangers; /* repo non peer (ne' autorita') che hanno annunciato
                      * una versione non arrivata: da qui non si scarica,
                      * la porta 'update --peers' lanciato da loro */
    Str_t to_merge;  /* repo che hanno annunciato una versione da unire */
    Str_t creator;   /* URL del creatore di T ("" = solo locale, D-103) */
    Str_t authority; /* URL dell'autorita' di T: l'ultimo sovrano, o il
                      * creatore (D-111) */
    int sovereign;   /* 1 se l'autorita' di T e' un sovrano diverso dal
                      * creatore */
    Oid_t auth_tip;  /* ultima versione vista sull'autorita' di T; len = 0
                      * se non c'e' (D-110, D-112) */
    AuthChange_t *changes; /* cambi di autorita' da decidere, e quelli gia'
                            * rifiutati (rejected = 1) (D-111) */
    size_t nchanges;
    int advanced;    /* 1 se T e' avanzata */
} Received_t;

#define RECEIVED_INIT { OIDLIST_INIT, STR_INIT, STR_INIT, STR_INIT, STR_INIT, \
                        STR_INIT, 0, { 0U, { 0U } }, NULL, 0U, 0 }

void received_free(Received_t *rv);

/*
 * Riceve per una voce collegata. all = 1: fetch anche da tutti i peer
 * (update --scan). GS_ERR_GIT se la catena non c'e' o git fallisce; un peer
 * irraggiungibile e' solo un avviso.
 */
eGsErr_t receive_entry(const Repo_t *r, const SharedEntry_t *e, GitBatch_t *b,
                       int all, Received_t *rv);

/*
 * Catena locale assente (D-112, reportIdea B-51): refs/shared/<id> si
 * ricrea dall'ultima versione vista in remoto (refs/shared-authority/<id>,
 * senza rete), altrimenti dal primo peer locale che l'ha. Un avviso dice
 * da dove. GS_OK e T in *t se ricreata; il cat-file b viene riaperto, perche'
 * quello aperto non vedrebbe la ref nuova. GS_ERR_GIT se nessuno l'ha.
 * receive_entry la chiama da se'.
 */
eGsErr_t receive_restore_chain(const SharedEntry_t *e, GitBatch_t *b, Oid_t *t);

/*
 * Come receive_entry, ma senza fetch e senza cambiare nulla (ref, avvisi,
 * config): dice soltanto quali cambi di autorita' aspettano una decisione
 * tra le versioni gia' nella repo (gli avvisi di versioni presenti e
 * l'ultima versione vista). Per gli hook, che non fanno rete (REQ-F-013).
 */
eGsErr_t receive_pending_auth(const Repo_t *r, const SharedEntry_t *e,
                              GitBatch_t *b, Received_t *rv);

/* Quanti cambi di autorita' aspettano una decisione (rejected = 0). */
size_t receive_pending_changes(const Received_t *rv);

/* 1 se l'autorita' della versione v e' tra quelle accettate (D-111). */
int receive_auth_accepted(const SharedEntry_t *e, GitBatch_t *b, const Oid_t *v);

/* 1 se il valore di una decisione vale per a: elenco compatto `keys`. */
int receive_auth_listed(const Str_t *keys, const Auth_t *a);

#endif /* GS_RECEIVE_H */
