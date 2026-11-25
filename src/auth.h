/*
 * auth.h - creatore e sovrani di una versione: l'autorita' (D-111).
 *
 * Il tree di una versione pubblicata in remoto ha, accanto a "content":
 *   - "creator": un blob con l'URL del creatore e un fine riga (D-103).
 *     Il creatore non cambia: nessun comando lo modifica;
 *   - "sovereigns": un blob con gli URL dei sovrani, uno per riga, con il
 *     fine riga, nell'ordine in cui sono stati insediati (git shared
 *     crown). Al piu' GS_SOVEREIGN_MAX righe; assente o vuoto = mai
 *     incoronato.
 * L'autorita' di una versione, da cui arrivano le versioni ufficiali e
 * verso cui vanno, e' l'ultimo sovrano, o il creatore se non ce ne sono.
 *
 * Gli URL vengono dalla catena, cioe' da altri: dato non fidato (D-107).
 * Un URL che comincia con '-', con caratteri di controllo o troppo lungo,
 * un elenco con una riga vuota o con piu' di GS_SOVEREIGN_MAX righe, un
 * blob troppo grande (controllato prima di leggerlo) rendono la versione
 * non valida: auth_read ritorna GS_ERR_FORMAT.
 *
 * Due versioni hanno la stessa autorita' se hanno lo stesso creatore e la
 * stessa autorita', confrontati in forma normale (remote_same, D-112).
 *
 * Accanto, la voce "heirs" (D-114): gli eredi dei sovrani, la linea di
 * successione (heirs.h). Viaggia con le altre e si unisce con loro
 * (auth_merge), ma non fa parte dell'autorita': auth_blobs_eq e auth_same
 * non la guardano.
 */
#ifndef GS_AUTH_H
#define GS_AUTH_H

#include <stddef.h>

#include "err.h"
#include "git.h"
#include "str.h"

/* Le due voci del tree di una versione, come blob: le si confronta e le si
 * copia nelle versioni nuove senza leggerle. */
typedef struct {
    Oid_t creator;    /* len = 0: nessuna voce (file solo locale) */
    Oid_t sovereigns; /* len = 0: nessuna voce (mai incoronato) */
    Oid_t heirs;      /* len = 0: nessuna voce (nessun erede, D-114) */
} AuthBlobs_t;

/* Le stesse voci lette. */
typedef struct {
    Str_t creator;    /* URL del creatore, "" = nessuno */
    Str_t sovereigns; /* URL dei sovrani, elenco compatto, dal primo */
    size_t n;         /* quanti sovrani */
} Auth_t;

#define AUTH_INIT { STR_INIT, STR_INIT, 0U }

void auth_free(Auth_t *a);

/* Blob delle tre voci della versione v: tre domande al cat-file, nessun
 * processo. */
void auth_blobs(GitBatch_t *b, const Oid_t *v, AuthBlobs_t *out);

/* 1 se le due versioni hanno la stessa autorita' blob per blob: creator e
 * sovereigns, non heirs. */
int auth_blobs_eq(const AuthBlobs_t *a, const AuthBlobs_t *b);

/* 1 se anche la voce heirs e' la stessa (le versioni che si riconoscono,
 * remote_find_own). */
int auth_blobs_eq_all(const AuthBlobs_t *a, const AuthBlobs_t *b);

/*
 * Testo di un blob scritto da altri, al piu' max byte, senza il fine riga
 * finale: GS_ERR_FORMAT se e' piu' grande (controllato prima di leggerlo) o
 * contiene un byte nullo. Un processo (cat-file blob) per blob non ancora
 * letto in questo processo. Per le voci della catena (anche heirs.h).
 */
eGsErr_t auth_blob_text(GitBatch_t *b, const Oid_t *blob, unsigned long max,
                        Str_t *out);

/*
 * Legge le voci: un processo (cat-file blob) per blob non ancora letto in
 * questo processo. GS_ERR_FORMAT se non sono valide (sopra): *a resta
 * vuota.
 */
eGsErr_t auth_read(GitBatch_t *b, const AuthBlobs_t *ab, Auth_t *a);

/* auth_blobs + auth_read. ab puo' essere NULL. */
eGsErr_t auth_of(GitBatch_t *b, const Oid_t *v, Auth_t *a, AuthBlobs_t *ab);

/* L'autorita': l'ultimo sovrano, o il creatore; "" senza creatore. */
const char *auth_authority(const Auth_t *a);

/*
 * Autorita' della versione v, per i comandi di rete: in url il suo URL
 * ("" senza creatore, o con voci non valide), in *sovereign 1 se e' un
 * sovrano diverso dal creatore. Per i messaggi: auth_role.
 */
eGsErr_t auth_target(GitBatch_t *b, const Oid_t *v, Str_t *url, int *sovereign);

/* "creator" o "sovereign", per i messaggi che nominano l'autorita'. */
const char *auth_role(int sovereign);

/* 1 se stesso creatore e stessa autorita', in forma normale. */
int auth_same(const Auth_t *a, const Auth_t *b);

/* 1 se stesso creatore, in forma normale ("" e' uguale solo a ""). */
int auth_creator_same(const Auth_t *a, const Auth_t *b);

/*
 * Voci di una versione che insedia `url` (crown): lo stesso creatore,
 * `url` in fondo all'elenco dei sovrani. Un URL gia' nell'elenco (in forma
 * normale) si sposta in fondo; oltre GS_SOVEREIGN_MAX righe si toglie la
 * piu' vecchia (REQ-NF-011). Scrive il blob nella repo. La voce heirs resta
 * quella di cur: gli eredi di un sovrano uscito li toglie il chiamante
 * (heirs_prune, D-114).
 */
eGsErr_t auth_crown(const Auth_t *cur, const AuthBlobs_t *curb, const char *url,
                    AuthBlobs_t *out);

/*
 * Unione a 3 vie della voce autorita' (D-111) per una versione con
 * genitori ours e theirs: base = l'autorita' della loro versione comune
 * (un processo merge-base, solo se le due sono diverse). Cambiata da un
 * solo lato -> quel lato; da entrambi allo stesso modo -> ours; da
 * entrambi in modo diverso -> *conflict = 1 e out = ours: decide il
 * chiamante (vince il lato la cui autorita' e' stata accettata, REQ-F-030).
 * La voce heirs (D-114) a parte, con la stessa base: cambiata da un solo
 * lato -> quel lato; da entrambi in modo diverso -> theirs, il lato
 * ricevuto, senza conflitto (gli eredi non decidono chi comanda).
 * theirs = NULL: le voci di ours.
 */
eGsErr_t auth_merge(GitBatch_t *b, const Oid_t *ours, const Oid_t *theirs,
                    AuthBlobs_t *out, int *conflict);

/*
 * Valore di una decisione su un'autorita' (authorityAccepted,
 * authorityRejected, REQ-F-030): l'URL dell'autorita'; con with_creator,
 * per un cambio di creatore, anche il creatore dopo un TAB, che in un URL
 * non puo' esserci (D-107).
 */
void auth_key(const Auth_t *a, int with_creator, Str_t *key);

/* 1 se il valore `key` decide l'autorita' di a: stessa autorita' e, se il
 * valore porta il creatore, stesso creatore (in forma normale). */
int auth_key_matches(const Auth_t *a, const char *key);

/* L'URL dell'autorita' scritta nel valore, senza l'eventuale creatore. */
void auth_key_authority(const char *key, Str_t *url);

#endif /* GS_AUTH_H */
