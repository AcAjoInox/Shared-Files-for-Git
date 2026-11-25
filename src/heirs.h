/*
 * heirs.h - gli eredi dei sovrani: la linea di successione (D-114).
 *
 * I primi due editori che, dopo la nascita del creatore o l'incoronazione
 * di un sovrano, fanno "git shared push" su di lui dal proprio remote
 * diventano i suoi eredi (Adamo, erede 1; Eva, erede 2). La voce "heirs"
 * del tree di una versione e' un blob con una riga per erede:
 *
 *     <url del sovrano padre> TAB <url dell'erede> LF
 *
 * Il padre e' il creatore o un URL di "sovereigns" (auth.h), scritto per
 * URL e non per posizione: in "sovereigns" il piu' vecchio esce. La
 * discendenza e' un albero binario con il creatore alla radice: al piu'
 * HEIRS_PER_PARENT eredi per padre, di casate diverse (remote_same_house),
 * nessun URL due volte, mai il creatore. Un erede che si incorona resta
 * dov'e': da li' partono i suoi eredi; per iscriversi invece non si deve
 * avere mai regnato.
 *
 * Dato scritto da altri (D-107): una riga malformata, un URL che non si
 * puo' usare, un blob troppo grande rendono la voce non valida, che vale
 * come nessun erede (heirs_read ritorna GS_ERR_FORMAT e la lista vuota).
 * Una riga il cui padre non e' piu' il creatore ne' un sovrano (uscito
 * dall'elenco), o oltre il limite del padre, o un doppione, si salta.
 *
 * Gli eredi dicono dove guardare se l'autorita' non risponde: l'ordine di
 * successione (heirs_order) e' la visita dell'albero a partire
 * dall'autorita'. Nessuno diventa autorita' da solo: incoronarsi resta
 * "git shared crown" (D-111).
 */
#ifndef GS_HEIRS_H
#define GS_HEIRS_H

#include <stddef.h>

#include "auth.h"
#include "err.h"
#include "git.h"
#include "str.h"

#define HEIRS_PER_PARENT 2U /* albero binario: Adamo ed Eva */
/* Righe lette al piu': 2 per padre, padri = creatore + GS_SOVEREIGN_MAX;
 * il doppio, per le righe orfane che una versione puo' ancora portare. */
#define HEIRS_LINES_MAX (4U * (GS_SOVEREIGN_MAX + 1U))

/* Coppie (padre, erede), nell'ordine delle righe: un elenco compatto in
 * cui ogni padre e' seguito dal suo erede. */
typedef struct {
    Str_t pairs;
    size_t n; /* quante coppie */
} Heirs_t;

#define HEIRS_INIT { STR_INIT, 0U }

void heirs_free(Heirs_t *h);

/*
 * Eredi del blob (len = 0: nessuno), validi per le voci a del creatore e
 * dei sovrani. GS_ERR_FORMAT se la voce non e' valida: h resta vuota.
 */
eGsErr_t heirs_read(GitBatch_t *b, const Oid_t *blob, const Auth_t *a,
                    Heirs_t *h);

/* Autorita' ed eredi della versione v (auth_of + heirs_read); una voce
 * heirs non valida vale come nessun erede. ab puo' essere NULL. */
eGsErr_t heirs_of(GitBatch_t *b, const Oid_t *v, Auth_t *a, AuthBlobs_t *ab,
                  Heirs_t *h);

/* Scrive nella repo il blob della voce; nessun erede: blob->len = 0. */
eGsErr_t heirs_write(const Heirs_t *h, Oid_t *blob);

/* Aggiunge la coppia in fondo. */
void heirs_add(Heirs_t *h, const char *parent, const char *heir);

/* Toglie le coppie il cui padre non e' piu' il creatore ne' un sovrano di
 * a (crown, quando il sovrano piu' vecchio esce). 1 se ha tolto qualcosa. */
int heirs_prune(Heirs_t *h, const Auth_t *a);

/* 1 se url e' un erede; in *parent (se non NULL) il suo padre. */
int heirs_find(const Heirs_t *h, const char *url, const char **parent);

/*
 * Iscrizione (D-114): la posizione (1 o 2) che il remote `own` prenderebbe
 * tra gli eredi dell'autorita' di a, 0 se non puo': own vuoto, uguale
 * all'autorita', al creatore o a un sovrano, gia' erede, nessun posto
 * libero, o della stessa casata dell'altro erede.
 */
unsigned heirs_can_join(const Auth_t *a, const Heirs_t *h, const char *own);

/*
 * Ordine di successione (D-114), senza rete: il sottoalbero dell'autorita'
 * (i suoi eredi in ordine, ognuno seguito dal proprio sottoalbero se ha
 * regnato); poi un livello sopra, al padre dell'autorita' o, se l'autorita'
 * non e' erede di nessuno, al sovrano che ha sostituito (poi il creatore):
 * il padre, se non e' il creatore, e i sottoalberi dei suoi eredi non
 * ancora visitati; e cosi' fino al creatore. Nessun URL due volte; ne'
 * l'autorita' ne' il creatore compaiono. In order un elenco compatto.
 */
void heirs_order(const Auth_t *a, const Heirs_t *h, Str_t *order);

/* Posizione di url nell'ordine (1 = il primo), 0 se non c'e'. */
unsigned heirs_position(const Str_t *order, const char *url);

#endif /* GS_HEIRS_H */
