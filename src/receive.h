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
 * Senza avvisi nessun processo. update --all fa il fetch da tutti i peer.
 *
 * In remoto (D-105): se T ha un creatore, ls-remote della sola ref sul
 * creatore e, se la sua punta manca qui, fetch della catena. La punta del
 * creatore entra tra le versioni ricevute come quella di un peer. Un
 * creatore irraggiungibile e' solo una nota: lo stato si ricava dalle
 * versioni che ci sono.
 */
#ifndef GS_RECEIVE_H
#define GS_RECEIVE_H

#include "err.h"
#include "git.h"
#include "shared.h"
#include "str.h"

typedef struct {
    OidList_t heads; /* versioni che non discendono da T e da cui T non
                      * discende: da unire (REQ-F-011) */
    Str_t missing;   /* repo che hanno annunciato una versione non arrivata
                      * (elenco compatto, senza doppioni) */
    Str_t to_merge;  /* repo che hanno annunciato una versione da unire */
    Str_t creator;   /* URL del creatore di T ("" = solo locale, D-103) */
    Oid_t creator_tip; /* punta di refs/shared/<id> sul creatore; len = 0
                        * se non c'e' o non si e' letta */
    int creator_unreachable; /* 1 se il creatore non ha risposto */
    int advanced;    /* 1 se T e' avanzata */
} Received_t;

#define RECEIVED_INIT { OIDLIST_INIT, STR_INIT, STR_INIT, STR_INIT, \
                        { 0U, { 0U } }, 0, 0 }

void received_free(Received_t *rv);

/*
 * Riceve per una voce collegata. all = 1: fetch anche da tutti i peer
 * (update --all). GS_ERR_GIT se la catena non c'e' o git fallisce; un peer
 * o il creatore irraggiungibili sono solo un avviso.
 */
eGsErr_t receive_entry(const Repo_t *r, const SharedEntry_t *e, GitBatch_t *b,
                       int all, Received_t *rv);

#endif /* GS_RECEIVE_H */
