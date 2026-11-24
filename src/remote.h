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
 * <url> <id>. L'ultima versione vista resta in refs/shared-authority/<id>,
 * come refs/remotes/: i comandi locali la usano come la versione di un
 * peer. Da quale URL e quando e' stata vista lo dice la chiave
 * shared.<id>.authoritySeen (D-112): il confronto con T vale solo se
 * quell'URL e' l'autorita' di T (oggi il creatore).
 *
 * Nei comandi di rete GIT_TERMINAL_PROMPT=0 (D-112, P-07): un push senza
 * permessi e' un rifiuto, non una richiesta di password.
 *
 * Il trasporto e' quello di git (push, fetch, credenziali, permessi):
 * nessun server nostro. Nessun dato locale va in remoto: solo la catena.
 */
#ifndef GS_REMOTE_H
#define GS_REMOTE_H

#include <stdint.h>

#include "auth.h"
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
    REMOTE_GOT = 0,       /* refs/shared-authority/<id> aggiornata */
    REMOTE_MISSING,       /* il creatore non ha refs/shared/<id> */
    REMOTE_UNREACHABLE    /* repo irraggiungibile o fetch fallito */
} eRemoteFetch_t;

/*
 * 1 se un URL scritto da altri (nella catena) si puo' usare (D-107): non
 * vuoto, al piu' GS_PATH_MAX caratteri, senza '-' in testa (diventerebbe
 * un'opzione di git) ne' caratteri di controllo.
 */
int remote_url_ok(const char *url);

/*
 * URL del remote `name` della repo corrente (git remote get-url).
 * GS_ERR_GIT se la repo non ha quel remote. Un path locale relativo
 * (git remote add origin ../srv.git) diventa assoluto rispetto a `top`,
 * la radice della repo: scritto nella catena, varrebbe per le altre repo
 * da un'altra cartella (D-112, P-08).
 */
eGsErr_t remote_url(const char *top, const char *name, Str_t *url);

/*
 * URL scritto dall'utente (crown, D-111): il nome di un remote della repo
 * (come remote_url), altrimenti l'URL stesso; un path locale relativo
 * diventa assoluto rispetto a `cwd`, la cartella da cui e' stato lanciato
 * il comando. Una parola senza separatori e' un remote che non c'e', non
 * un path (per una cartella: "./x"). GS_ERR_FORMAT se l'URL non si puo'
 * usare (remote_url_ok).
 */
eGsErr_t remote_arg_url(const char *top, const char *cwd, const char *arg,
                        Str_t *url);

/*
 * 1 se a e b sono la stessa repo remota (D-112, P-04): si confrontano in
 * forma normale. Rete: host senza utente ne' porta, path senza "/" e
 * ".git" finali, tutto in minuscolo, quindi https://github.com/luca/A,
 * git@github.com:luca/A.git e ssh://git@github.com/luca/A sono uguali.
 * Path locale (anche file://): separatore '/', senza "/" finale; su
 * Windows senza distinzione di maiuscole.
 */
int remote_same(const char *a, const char *b);

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
 * refs/shared-authority/<id> (D-110, D-112): un fetch per tutte, con i refspec
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

/* Ultima versione vista: refs/shared-authority/<id> (D-112; fino a
 * v0.8.0 refs/shared-creator/<id>, che prune toglie). */
void remote_seen_ref(Str_t *ref, const char *id);

/*
 * Scrive shared.<id>.authoritySeen = "<secondi dal 1970> <url>": la
 * versione in refs/shared-authority/<id> e' stata vista su `url` adesso
 * (D-112). Una sola chiave, quindi URL e data cambiano insieme. Se
 * mem_url / mem_at non sono NULL vi copia gli stessi valori, per chi li
 * usa nello stesso processo.
 */
eGsErr_t remote_seen_write(const char *id, const char *url, Str_t *mem_url,
                           unsigned long *mem_at);

/* Legge il valore di authoritySeen. 0 se non ha la forma giusta. */
int remote_seen_parse(const char *value, Str_t *url, unsigned long *at);

/*
 * P-03 (D-112): dopo un push riuscito e un update-ref fallito la versione
 * e' sul remoto ma non qui, e un nuovo primo push la ricreerebbe con
 * un'altra data, rifiutata. Legge la punta di refs/shared/<id> su `url` e
 * la riconosce come la propria se ha per genitori T e p2 (NULL: solo T),
 * lo stesso contenuto di T e le voci `auth` (creator e sovereigns). Vale
 * anche per insediare un sovrano (D-111), dove p2 e' la punta dell'URL.
 * 1 e la versione in *v se riconosciuta.
 */
int remote_find_own(GitBatch_t *b, const char *url, const char *id,
                    const Oid_t *t, const Oid_t *p2, const AuthBlobs_t *auth,
                    Oid_t *v);

#endif /* GS_REMOTE_H */
