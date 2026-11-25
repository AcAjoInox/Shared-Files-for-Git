/*
 * cmd_common.h - parti comuni ai sottocomandi.
 */
#ifndef GS_CMD_COMMON_H
#define GS_CMD_COMMON_H

#include <stddef.h>

#include "err.h"
#include "git.h"
#include "msg.h"
#include "receive.h"
#include "shared.h"
#include "state.h"

/* Opzioni ammesse da cmd_parse. */
#define CMD_OPT_COMMIT 0x1U /* -c, --commit, -m <msg> (D-007) */
#define CMD_OPT_FORCE  0x2U /* -f, --force */
#define CMD_OPT_DRY_RUN 0x4U /* -n, --dry-run */
#define CMD_OPT_ABORT  0x8U /* --abort (update, D-093) */
#define CMD_OPT_ALL    0x10U /* --all (update, D-099) */
#define CMD_OPT_MERGE  0x20U /* --merge (link, D-029, D-102) */
#define CMD_OPT_ADOPT  0x40U /* --adopt (link, D-064) */
#define CMD_OPT_CHOOSE 0x100U /* --ours, --theirs (update, D-108) */

typedef struct {
    const char *message; /* -m; NULL = messaggio standard */
    unsigned commit : 1U;
    unsigned force : 1U;
    unsigned dry_run : 1U;
    unsigned abort : 1U;
    unsigned all : 1U;
    unsigned merge : 1U;
    unsigned adopt : 1U;
    unsigned ours : 1U;
    unsigned theirs : 1U;
    unsigned dashdash : 1U; /* le opzioni sono finite con "--" */
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
 * della sorgente per link, NULL altrimenti; remote = 1 se src e' l'URL di
 * una repo remota (link remoto, D-106: nessuna membership); commit = HEAD
 * al comando per unlink (D-081), NULL altrimenti. Registra anche il branch
 * corrente, su cui si cerchera' il commit di conferma. Tutto nella sola
 * chiave shared.<id>.pending, con una scrittura (D-113). */
eGsErr_t cmd_set_pending(const Repo_t *r, const char *id, const char *op,
                         const char *path, const char *src, int remote,
                         const char *commit);

/*
 * Sezione [shared "<name>"] in .gitshared (id, path, branch corrente come
 * default, D-057); crea il file con l'intestazione se manca. In caso di
 * errore .gitshared resta com'era.
 */
eGsErr_t cmd_write_gitshared(const Repo_t *r, const char *name,
                             const char *id, const char *path);

/*
 * Toglie la sezione [shared "<name>"] da .gitshared; se non resta nessuna
 * sezione, toglie il file. Poi lo mette in staging (anche la
 * cancellazione). Usata da unlink e da link --adopt.
 */
eGsErr_t cmd_drop_gitshared(const Repo_t *r, const char *name);

/*
 * Controlla che `path` sia un file tracciato, senza conflitti, regolare
 * (100644 o 100755), con un solo processo (ls-files). dir = NULL: path
 * dell'utente, relativo alla cartella corrente; altrimenti la cartella da
 * cui leggerlo (la radice, per un path relativo alla radice). Se va bene
 * scrive in full il path relativo alla radice; altrimenti stampa l'errore
 * (con `path` com'e' scritto) e ritorna GS_ERR_USAGE.
 */
eGsErr_t cmd_resolve_tracked(const char *dir, const char *path, Str_t *full);

/* 1 se il path (relativo alla radice) esiste: su disco o nell'index. */
int cmd_path_exists(const Repo_t *r, GitBatch_t *b, const char *path);

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
 * stato ha usato (reportIdea B-31), con il creatore di T (D-103), poi
 * scrive l'avviso di T in tutti i peer (D-099); un peer con .git file
 * riceve con il push. Senza create (file allineato) si avvisano solo i peer
 * che non hanno T ne' il suo avviso: se ce l'hanno tutti, "gia' pubblicata"
 * e nient'altro. tip = T come l'ha letta lo stato; all'uscita, la T
 * pubblicata. GS_EXIT_OK, oppure GS_EXIT_ERROR se la versione non si crea
 * o il push di ripiego e' rifiutato (messaggi stampati).
 */
int cmd_publish_entry(const Repo_t *r, const SharedEntry_t *e, GitBatch_t *b,
                      const StateInfo_t *create, Oid_t *tip);

/* Path dell'utente (relativo alla cartella corrente) -> relativo alla
 * radice. 0 se assoluto o fuori dalla repo. */
int cmd_repo_path(const Repo_t *r, const char *user_path, Str_t *out);

/*
 * Voci su cui lavorano publish e update: tutte quelle collegate, o quella
 * del path dato (NULL = tutte). Ritorna 0 se il path non e' un file shared
 * (errore stampato). sel[i] = 1 per le voci scelte.
 */
int cmd_select(Repo_t *r, const char *user_path, unsigned char *sel);

/*
 * Ultima versione vista sull'autorita' per le voci scelte (D-110, D-111):
 * un fetch per autorita', con tutte le sue catene (remote_fetch_seen).
 * res[i] = CMD_NO_CREATOR se T non ha un creatore, altrimenti un
 * eRemoteFetch_t; urls[i] = URL dell'autorita' di T ("" = nessuna), r->n
 * elementi inizializzati dal chiamante; sov[i] (se sov non e' NULL) = 1 se
 * l'autorita' e' un sovrano (auth_role). Dopo, il cat-file va riaperto:
 * deve vedere le ref nuove.
 */
#define CMD_NO_CREATOR 0xFFU
void cmd_fetch_creators(Repo_t *r, GitBatch_t *b, const unsigned char *sel,
                        unsigned char *res, Str_t *urls, unsigned char *sov);

/*
 * Il cuore di update (REQ-F-009) per le voci scelte, dopo le opzioni e
 * --abort / --ours / --theirs: ricezione, unioni, applicazioni, commit con
 * -c / -m e messaggi finali. Lo usa anche pull (REQ-F-028). rc: il codice
 * di uscita fin qui; ritorna quello nuovo. *batch_ok = 0 se il cat-file e'
 * stato chiuso.
 */
int cmd_update_run(Repo_t *r, GitBatch_t *b, const unsigned char *sel,
                   CmdOpts_t *o, int rc, int *batch_ok);

/*
 * Cambi di autorita' da decidere (D-111, REQ-F-030) ricevuti in rv. warn
 * = 0: righe di status ("authority -> <url>, <provenienza> (version ...)",
 * il creatore nuovo se cambia, il comando per decidere); warn = 1: un
 * avviso per cambio (update, pull, hook).
 */
void cmd_print_auth_changes(const SharedEntry_t *e, const Received_t *rv, int warn);

/* Provenienza ("crowned by the local peer <path>", ...) e versione
 * ("<oid breve>, <autore>, <data>") di un cambio di autorita'. */
void cmd_auth_describe(const AuthChange_t *c, Str_t *from, Str_t *info);

/* Argomento di crown --accept per un cambio: "<path>", o "<url> <path>"
 * se ce n'e' piu' d'uno. */
void cmd_auth_how(const SharedEntry_t *e, const Received_t *rv, size_t k, Str_t *how);

/*
 * Visita della linea di successione (D-114, heirs.h, cmd_crown.c): per un
 * file la cui autorita' non risponde, la catena di ogni erede nell'ordine
 * di successione, fino al primo che dichiara un'autorita' nuova:
 *   - HEIR_VISIT_FOUND: un erede di T (con apply: lo si segue senza
 *     domande, HEIR_VISIT_FOLLOWED, e T avanza come con crown);
 *   - HEIR_VISIT_OTHER: un'autorita' che non e' un erede: solo con crown;
 *   - HEIR_VISIT_CREATOR: nessun erede, ma il creatore risponde (e
 *     l'autorita' era un sovrano): crown --restore;
 *   - HEIR_VISIT_NONE: nessuno.
 * where: l'indirizzo che l'ha detto; to: l'autorita' nuova; pos: la sua
 * posizione nell'ordine. Il cat-file viene riaperto: ritorna 0 se non ci
 * riesce (errore fatale stampato).
 */
typedef enum {
    HEIR_VISIT_NONE = 0,
    HEIR_VISIT_FOUND,
    HEIR_VISIT_FOLLOWED,
    HEIR_VISIT_OTHER,
    HEIR_VISIT_CREATOR,
    HEIR_VISIT_ERROR
} eHeirVisit_t;

typedef struct {
    eHeirVisit_t kind;
    Str_t where;
    Str_t to;
    unsigned pos;
} HeirVisit_t;

#define HEIRVISIT_INIT { HEIR_VISIT_NONE, STR_INIT, STR_INIT, 0U }

void heirvisit_free(HeirVisit_t *v);
int cmd_heirs_visit(Repo_t *r, SharedEntry_t *e, GitBatch_t *b, int apply,
                    HeirVisit_t *out);

/*
 * Ritorno del creatore (D-114): per un file la cui autorita' e' un sovrano,
 * *back = 1 se il creatore risponde, ha il file e la sua linea non porta
 * all'autorita' di T. Un fetch. Ritorna 0 se il cat-file non si riapre.
 */
int cmd_creator_back(Repo_t *r, SharedEntry_t *e, GitBatch_t *b, int *back);

/* Avviso per publish/update lanciati a mano su un branch sospeso (atto
 * esplicito: si fa); se nessun branch attivo esiste, anche il rimedio. */
void cmd_warn_suspended(const SharedEntry_t *e, BranchNames_t *bn);

#endif /* GS_CMD_COMMON_H */
