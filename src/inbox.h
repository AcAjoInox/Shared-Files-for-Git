/*
 * inbox.h - avvisi di nuove versioni tra repo locali (D-099).
 *
 * In locale chi pubblica non spinge la catena nei peer: scrive in ognuno
 * un avviso, <peer>/.git/shared/inbox/<id>/<oid della versione>, che
 * contiene una riga, la radice della repo che pubblica. Il trasferimento
 * lo fa chi riceve, al suo primo comando che ricava lo stato (receive.h).
 *
 * L'avviso e' un suggerimento: dice dove fare il fetch, non entra nello
 * stato (D-041), si puo' perdere senza danni e non e' mai versionato.
 * Si scrive in un file temporaneo nella stessa cartella e lo si rinomina
 * (REQ-NF-005). Nessun processo: solo file system.
 *
 * Solo per le repo con la cartella .git: un peer con .git file (worktree
 * secondario, --separate-git-dir) riceve con il push (reportIdea B-50).
 */
#ifndef GS_INBOX_H
#define GS_INBOX_H

#include "err.h"
#include "git.h"
#include "peers.h"
#include "shared.h"
#include "str.h"

typedef struct {
    OidList_t oids; /* versioni annunciate */
    Str_t from;     /* per ognuna, nello stesso ordine, la repo che l'ha
                     * annunciata ("" se l'avviso non la dice) */
} Inbox_t;

#define INBOX_INIT { OIDLIST_INIT, STR_INIT }

void inbox_free(Inbox_t *in);

/* Cartella degli avvisi della repo con radice top: <top>/.git/shared/inbox.
 * 0 se <top>/.git non e' una cartella: la repo non riceve avvisi. */
int inbox_root(const char *top, Str_t *dir);

/*
 * Avvisi di un ID nella repo corrente. Si ignorano i nomi che non sono
 * un oid della lunghezza della repo (REQ-NF-009), compresi i file
 * temporanei di una scrittura interrotta.
 */
void inbox_read(const Repo_t *r, const char *id, Inbox_t *in);

/* Cancella un avviso (non c'era: niente). */
void inbox_drop(const Repo_t *r, const char *id, const Oid_t *oid);

/* Cancella la cartella degli avvisi di un ID, con il contenuto. */
void inbox_remove(const char *top, const char *id);

/* Nella repo con radice top, cancella gli avvisi dell'ID scritti a nome di
 * una delle repo di `from` (prune --one-way, D-118). */
void inbox_withdraw(const char *top, const char *id, const Str_t *from);

/* Nomi delle cartelle per ID (prune). Nessuna cartella: elenco vuoto. */
void inbox_ids(const char *top, Str_t *ids);

/*
 * Scrive l'avviso di oid, a nome di from, nella repo con radice top (anche
 * la repo stessa: una versione da unire trovata con update --scan, che non
 * va dimenticata). Uno che c'e' gia' non si riscrive. 0 = c'e'.
 */
int inbox_add(const char *top, const char *id, const Oid_t *oid,
              const char *from);

/* 1 se nel peer c'e' l'avviso di quella versione (status --peers). */
int inbox_has(const char *peer, const char *id, const Oid_t *oid);

/*
 * Scrive l'avviso di tip in ogni peer, a nome di self. Un avviso che c'e'
 * gia' non si riscrive. Un peer con .git file finisce in `push`, da
 * raggiungere con il push; un peer senza .git, o in cui non si scrive, e'
 * irraggiungibile (REQ-F-008), con un avviso a nome di label.
 */
void inbox_notify(const char *label, const char *id, const Str_t *peers,
                  const char *self, const Oid_t *tip, PeerTally_t *t,
                  Str_t *push);

#endif /* GS_INBOX_H */
