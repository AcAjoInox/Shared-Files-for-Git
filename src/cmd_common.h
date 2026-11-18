/*
 * cmd_common.h - parti comuni ai sottocomandi.
 */
#ifndef GS_CMD_COMMON_H
#define GS_CMD_COMMON_H

#include <stddef.h>

#include "err.h"
#include "git.h"
#include "msg.h"
#include "shared.h"
#include "state.h"

/* Opzioni ammesse da cmd_parse. */
#define CMD_OPT_COMMIT 0x1U /* -c, --commit, -m <msg> (D-007) */
#define CMD_OPT_FORCE  0x2U /* -f, --force */
#define CMD_OPT_DRY_RUN 0x4U /* -n, --dry-run */
#define CMD_OPT_ABORT  0x8U /* --abort (update, D-093) */
#define CMD_OPT_ALL    0x10U /* --all (update, D-099) */

typedef struct {
    const char *message; /* -m; NULL = messaggio standard */
    unsigned commit : 1U;
    unsigned force : 1U;
    unsigned dry_run : 1U;
    unsigned abort : 1U;
    unsigned all : 1U;
} CmdOpts_t;

/*
 * Opzioni comuni, fino al primo argomento che non e' un'opzione (o dopo
 * "--"). Ritorna l'indice del primo argomento posizionale; -1 se le
 * opzioni non vanno (errore e uso gia' stampati).
 */
int cmd_parse(int argc, char **argv, unsigned allowed, eMsgId_t usage,
              CmdOpts_t *o);

/*
 * Completa le operazioni in attesa (repo_complete_pending) e, se si e'
 * chiusa l'ultima, toglie il blocco del post-commit (D-100).
 */
eGsErr_t cmd_complete(Repo_t *r, GitBatch_t *b, int may_discard);

/*
 * Apre la repo e il processo cat-file e completa le operazioni in attesa.
 * GS_EXIT_OK oppure il codice di uscita da restituire (messaggio stampato).
 * Se riesce, il chiamante chiude con git_batch_close e repo_close.
 */
int cmd_open(Repo_t *r, GitBatch_t *b);

/* Operazione in attesa in .git/config (REQ-F-023, D-077): op = "set" |
 * "link" | "unlink"; path = path del file (per i messaggi); src = radice
 * della sorgente per link, NULL altrimenti; commit = HEAD al comando per
 * unlink (D-081), NULL altrimenti. Registra anche il branch corrente, su
 * cui si cerchera' il commit di conferma. */
eGsErr_t cmd_set_pending(const Repo_t *r, const char *id, const char *op,
                         const char *path, const char *src,
                         const char *commit);

/*
 * Sezione [shared "<name>"] in .gitshared (id, path, branch corrente come
 * default, D-057); crea il file con l'intestazione se manca. In caso di
 * errore .gitshared resta com'era.
 */
eGsErr_t cmd_write_gitshared(const Repo_t *r, const char *name,
                             const char *id, const char *path);

/* Mette in staging un path relativo alla radice (update-index), anche
 * la sua cancellazione se non c'e' piu' nel working tree. */
eGsErr_t cmd_stage(const Repo_t *r, const char *path);

/* Porta un blob nell'index e nel working tree, senza passare il contenuto
 * per l'estensione (D-042): update-index --cacheinfo + checkout-index. */
eGsErr_t cmd_write_file(const Repo_t *r, const char *path, const Oid_t *blob);

/* 1 se il working tree del file e' diverso dall'index. */
int cmd_worktree_differs(const Repo_t *r, const char *path);

/*
 * 1 se il file ha modifiche dell'utente non committate (REQ-F-010): il
 * working tree e' diverso dall'index, oppure l'index e' diverso da HEAD e
 * da ogni versione della catena. Una versione applicata da update e non
 * ancora committata non e' una modifica locale (D-076). Controlla anche
 * l'index: una modifica in staging e' protetta (reportIdea B-30).
 */
int cmd_has_local_changes(const Repo_t *r, const SharedEntry_t *e, GitBatch_t *b);

/* Commit dedicato dei soli path dati, relativi alla radice (D-007, A-29). */
eGsErr_t cmd_commit(const Repo_t *r, const char *message, const Str_t *paths);

/*
 * Dopo un commit lanciato dal comando: rilegge la repo e completa le
 * attese con un cat-file nuovo (che veda il nuovo HEAD). Si rilegge tutto
 * perche' il post-commit puo' aver gia' completato delle attese (D-100):
 * i puntatori alle voci di prima non valgono piu'. GS_EXIT_OK o il codice
 * di uscita (messaggio stampato); se non riesce, repo e cat-file sono
 * chiusi.
 */
int cmd_reopen(Repo_t *r, GitBatch_t *b);

/*
 * Pubblica un file: con create != NULL crea prima la nuova versione (stato
 * "da pubblicare", la base e' T: D-065) dal blob di HEAD e dalla T che lo
 * stato ha usato (reportIdea B-31), poi scrive l'avviso di T in tutti i
 * peer (D-099); un peer con .git file riceve con il push. tip = T come
 * l'ha letta lo stato. GS_EXIT_OK, oppure GS_EXIT_ERROR se la versione non
 * si crea o il push di ripiego e' rifiutato (messaggi stampati).
 */
int cmd_publish_entry(const Repo_t *r, const SharedEntry_t *e,
                      const StateInfo_t *create, const Oid_t *tip);

/* Path dell'utente (relativo alla cartella corrente) -> relativo alla
 * radice. 0 se assoluto o fuori dalla repo. */
int cmd_repo_path(const Repo_t *r, const char *user_path, Str_t *out);

/*
 * Voci su cui lavorano publish e update: tutte quelle collegate, o quella
 * del path dato (NULL = tutte). Ritorna 0 se il path non e' un file shared
 * (errore stampato). sel[i] = 1 per le voci scelte.
 */
int cmd_select(Repo_t *r, const char *user_path, unsigned char *sel);

/* Avviso per publish/update lanciati a mano su un branch sospeso (atto
 * esplicito: si fa); se nessun branch attivo esiste, anche il rimedio. */
void cmd_warn_suspended(const SharedEntry_t *e, BranchNames_t *bn);

#endif /* GS_CMD_COMMON_H */
