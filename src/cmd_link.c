/*
 * cmd_link.c - "git shared link [-c | -m <msg>] [-f | --merge] [--adopt]
 * [--] <sorgente> [<dest>]" (REQ-F-002, D-005, D-028, D-059).
 *
 * Al comando: fetch della catena dalla sorgente in una ref temporanea,
 * file scritto dal blob dell'ultima versione, sezione in .gitshared, tutto
 * in staging; operazione in attesa con la radice della sorgente. Al commit
 * (post-commit, D-100; repo_complete_pending): catena, membership,
 * annuncio ai peer, avvisi delle versioni pubblicate nel frattempo.
 *
 * Una sorgente con il set (o il link) ancora in attesa lo completa prima
 * del fetch (D-100): il suo post-commit puo' non essere girato.
 *
 * Destinazione che esiste gia' (D-062): il file non si tocca e lo stato si
 * ricava dopo il commit come per ogni file shared. Si controlla solo che
 * una base ci sia: il contenuto nell'index e' una versione della catena, o
 * una versione compare nella storia del file. Senza base (D-029, D-102):
 * errore; -f sovrascrive con l'ultima versione; --merge collega lo stesso,
 * il file entra *divergente* e update lo unisce con la base vuota.
 *
 * Destinazione gia' shared con un altro ID (D-064): errore; con --adopt la
 * repo prende l'ID della sorgente. Due attese chiuse dallo stesso commit:
 * unlink della vecchia identita' (se collegata) e link della nuova. Il file
 * non si tocca, come per una destinazione che esiste.
 *
 * Ri-aggancio (D-043, D-079): se .gitshared dichiara gia' lo stesso ID ma
 * la repo non ha dati locali ("scollegato": un clone, o un'attesa scartata
 * durante un git stash), link non da' errore. Riprende nome e path della
 * sezione, rifa' fetch, attesa e hook, e non tocca il file: lo stato si
 * ricava poi come per ogni file shared. Se la sezione e' gia' committata
 * il collegamento vale subito.
 *
 * Forma remota (D-106): "link <url> <id> [<dest>]", riconosciuta dal
 * secondo argomento, che e' un ID. Fetch della sola catena dalla repo
 * remota (il creatore, o una sua copia: dentro c'e' scritto il creatore);
 * la catena deve avere la voce "creator", cioe' essere stata pubblicata in
 * remoto. Il nome viene dal messaggio delle versioni ("shared: <nome>"),
 * la destinazione di default e' il nome. Al commit solo la catena: nessuna
 * membership, il creatore non registra nulla. Vale anche come ri-aggancio
 * dopo un clone, con l'URL di origin (ideeRemote §4.4).
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "chain.h"
#include "cmd.h"
#include "cmd_common.h"
#include "hooks.h"
#include "path.h"
#include "peers.h"
#include "remote.h"
#include "state.h"
#include "sys.h"

/* La sorgente: repo, path relativo e voce in .gitshared. */
typedef struct {
    Str_t top;
    Str_t rel;
    Str_t objfmt;
    Repo_t shared; /* solo le voci del suo .gitshared */
} Source_t;

/* La vecchia identita' della destinazione, per --adopt (D-064). */
typedef struct {
    Str_t name;
    char id[GS_ID_HEX_LEN + 1U];
    char head[OID_HEX_SIZE]; /* HEAD al comando: commit dell'unlink */
    int linked;              /* ha dati locali: serve un unlink in attesa */
} Adopt_t;

static void source_free(Source_t *s)
{
    str_free(&s->top);
    str_free(&s->rel);
    str_free(&s->objfmt);
    repo_close(&s->shared);
}

#ifdef _WIN32
/* La voce con il path uguale senza distinzione di maiuscole (ASCII), se e'
 * una sola; NULL altrimenti. */
static const SharedEntry_t *find_path_nocase(const Repo_t *r, const char *path)
{
    const SharedEntry_t *hit = NULL;
    const char *a;
    const char *b;
    size_t i;
    unsigned n = 0U;

    for (i = 0U; i < r->n; i++) {
        a = str_cstr(&r->v[i].path);
        b = path;
        while ((*a != '\0') && (tolower((unsigned char)*a) == tolower((unsigned char)*b))) {
            a++;
            b++;
        }
        if ((*a == '\0') && (*b == '\0')) {
            hit = &r->v[i];
            n++;
        }
    }
    return (n == 1U) ? hit : NULL;
}
#endif

/* Risale dalla path del file alla repo sorgente e alla sua voce (D-005). */
static const SharedEntry_t *find_source(const Repo_t *r, const char *user_path,
                                        Source_t *s)
{
    Str_t dir = STR_INIT;
    Str_t prefix = STR_INIT;
    Str_t file = STR_INIT;
    const SharedEntry_t *e = NULL;
    const char *name;

    path_split(user_path, &dir, &name);
    if ((name[0] == '\0') ||
        (repo_locate(str_cstr(&dir), &s->top, &prefix, &s->objfmt) != GS_OK) ||
        !path_join_norm(str_cstr(&prefix), name, &s->rel)) {
        msg_error(MSG_ERR_SRC_NOT_REPO, user_path);
        goto exit;
    }
    if (str_eq(&s->top, str_cstr(&r->top))) {
        msg_error(MSG_ERR_SAME_REPO);
        goto exit;
    }
    /* Tutti i peer usano lo stesso algoritmo di hash (REQ-NF-009). */
    if (!str_eq(&s->objfmt, str_cstr(&r->objfmt))) {
        msg_error(MSG_ERR_HASH_MISMATCH, str_cstr(&s->objfmt),
                  str_cstr(&r->objfmt));
        goto exit;
    }
    str_addf(&file, "%s/.gitshared", str_cstr(&s->top));
    if (repo_load_gitshared(&s->shared, str_cstr(&file), 0, 0) == GS_OK) {
        e = repo_find_path(&s->shared, str_cstr(&s->rel));
#ifdef _WIN32
        /* C-12: il file system di Windows non distingue le maiuscole:
         * COMMON.H e' common.h, se e' l'unico con quel nome. */
        if (e == NULL) {
            e = find_path_nocase(&s->shared, str_cstr(&s->rel));
        }
        if (e != NULL) {
            str_reset(&s->rel);
            str_add(&s->rel, str_cstr(&e->path)); /* il path dichiarato */
        }
#endif
    }
    if ((e == NULL) || !e->valid) {
        msg_error(MSG_ERR_SRC_NOT_SHARED, user_path); /* D-019 */
        e = NULL;
    }

exit:
    str_free(&file);
    str_free(&prefix);
    str_free(&dir);
    return e;
}

/*
 * Nome del file shared dal messaggio di una versione: "shared: <nome>"
 * (chain_commit). Si legge con cat-file (plumbing). Senza il prefisso, o
 * con un nome vuoto, vale l'ID.
 */
static void version_name(const Oid_t *v, const char *id, Str_t *name)
{
    static const char prefix[] = "shared: ";
    const char *args[] = { "cat-file", "commit", NULL, NULL };
    char hex[OID_HEX_SIZE];
    Str_t out = STR_INIT;
    Str_t raw = STR_INIT;
    const char *subj;
    const char *end;

    oid_to_hex(v, hex);
    args[2] = hex;
    str_reset(name);
    if (git_run(args, 0U, &out) == GS_OK) {
        subj = strstr(str_cstr(&out), "\n\n");
        if ((subj != NULL) &&
            (strncmp(subj + 2, prefix, sizeof prefix - 1U) == 0)) {
            subj += 2U + (sizeof prefix - 1U);
            end = strchr(subj, '\n');
            str_addn(&raw, subj, (end != NULL) ? (size_t)(end - subj) : strlen(subj));
            /* scritto da altri: niente controlli per il terminale (S-07) */
            str_add_printable(name, str_cstr(&raw));
        }
    }
    if (name->len == 0U) {
        str_add(name, id);
    }
    str_free(&raw);
    str_free(&out);
}

/*
 * S-08: senza <dest> il path lo sceglierebbe chi ha scritto la catena
 * (.github/workflows/ci.yml, .gitattributes, Makefile in una cartella...).
 * Vale solo un nome di file semplice: niente separatori, niente '.' in
 * testa, solo ASCII stampabile, nessun carattere che Windows non ammette
 * in un nome (anche il '?' messo da str_add_printable al posto di un
 * carattere non stampabile).
 */
static int name_is_simple(const char *name)
{
    const unsigned char *c = (const unsigned char *)name;

    if ((*c == '\0') || (*c == '.')) {
        return 0;
    }
    for (; *c != '\0'; c++) {
        if ((*c < 0x20U) || (*c >= 0x7FU) || (strchr("/\\:<>\"|?*", (int)*c) != NULL)) {
            return 0;
        }
    }
    return 1;
}

/*
 * Forma remota (D-106): catena da <url> in refs/shared-incoming/<id>/0,
 * che deve avere un creatore; nome dal messaggio della versione. src->top
 * riceve l'URL: e' la sorgente per i messaggi e per l'attesa. GS_EXIT_OK o
 * il codice di uscita (messaggio stampato).
 */
static int remote_source(const Repo_t *r, GitBatch_t *b, const char *arg,
                         const char *id, Source_t *s, Str_t *name)
{
    Str_t ref = STR_INIT;
    Str_t creator = STR_INIT;
    Str_t cwd = STR_INIT;
    Str_t resolved = STR_INIT;
    const char *url;
    Oid_t tip;
    int sovereign = 0;
    int rc = GS_EXIT_ERROR;

    /* AB-06: l'argomento si risolve come in crown: il nome di un remote
     * (il suo URL), un path relativo alla cartella corrente. Da qui in poi
     * vale l'URL risolto: fetch, confronto con l'autorita', messaggi e
     * sorgente dell'attesa. */
    str_addf(&cwd, "%s/%s", str_cstr(&r->top), str_cstr(&r->prefix));
    if (remote_arg_url(str_cstr(&r->top), str_cstr(&cwd), arg, &resolved) != GS_OK) {
        msg_error(MSG_ERR_CROWN_BAD_URL, arg);
        goto exit;
    }
    url = str_cstr(&resolved);
    peers_incoming_ref(&ref, id, 0U);
    if ((remote_fetch(url, id, str_cstr(&ref)) != GS_OK) ||
        (git_batch_query(b, str_cstr(&ref), &tip) != GIT_OBJ_COMMIT)) {
        msg_error(MSG_ERR_REMOTE_FETCH, id, url);
        goto exit;
    }
    if ((remote_creator_url(b, &tip, &creator) != GS_OK) || (creator.len == 0U)) {
        msg_error(MSG_ERR_NO_CREATOR, id, url);
        str_reset(&creator);
        str_addz(&creator, str_cstr(&ref));
        (void)git_delete_refs(&creator);
        goto exit;
    }
    /* Da una copia (D-111): chi governa il file, perche' il file seguira'
     * l'autorita' scritta nella catena, non l'URL dato. */
    if ((auth_target(b, &tip, &creator, &sovereign) == GS_OK) && (creator.len > 0U) &&
        !remote_same(str_cstr(&creator), url)) {
        printf(msg(MSG_LINK_COPY), url, str_cstr(&creator));
        putchar('\n');
    }
    version_name(&tip, id, name);
    str_add(&s->top, url);
    rc = GS_EXIT_OK;

exit:
    str_free(&resolved);
    str_free(&cwd);
    str_free(&creator);
    str_free(&ref);
    return rc;
}

/* 1 se il file c'e' e si apre. */
static int file_exists(const char *path)
{
    FILE *f = sys_fopen(path, "rb");

    if (f != NULL) {
        (void)fclose(f);
    }
    return f != NULL;
}

/*
 * C-21: un antenato della destinazione che e' un file (nel working tree o
 * nell'index): update-index fallirebbe dopo la scrittura dell'attesa e di
 * .gitshared. 1 e l'antenato in which.
 */
static int parent_is_file(const Repo_t *r, GitBatch_t *b, const char *dest,
                          Str_t *which)
{
    Str_t disk = STR_INIT;
    Str_t name = STR_INIT;
    const char *slash;
    Oid_t oid;
    FILE *f;
    int found = 0;

    for (slash = strchr(dest, '/'); (slash != NULL) && (found == 0);
         slash = strchr(slash + 1, '/')) {
        str_reset(which);
        str_addn(which, dest, (size_t)(slash - dest));
        str_reset(&disk);
        str_addf(&disk, "%s/%s", str_cstr(&r->top), str_cstr(which));
        str_reset(&name);
        str_addf(&name, ":%s", str_cstr(which));
        f = sys_is_dir(str_cstr(&disk)) ? NULL : sys_fopen(str_cstr(&disk), "rb");
        found = (f != NULL) || (git_batch_query(b, str_cstr(&name), &oid) == GIT_OBJ_BLOB);
        if (f != NULL) {
            (void)fclose(f);
        }
    }
    str_free(&name);
    str_free(&disk);
    return found;
}

/*
 * C-21: la scrittura e' fallita dopo l'attesa e la sezione di .gitshared.
 * Si toglie la sezione (e .gitshared, se non dichiara altro e non c'era) e
 * l'attesa, come se il comando non fosse partito.
 */
static void undo_link(const Repo_t *r, const char *id, const char *name,
                      int gitshared_new)
{
    const char *cfg[] = { "config", "-f", NULL, "--remove-section", NULL, NULL };
    const char *pend[] = { "config", "--unset-all", NULL, NULL };
    Str_t file = STR_INIT;
    Str_t key = STR_INIT;

    str_addf(&file, "%s/.gitshared", str_cstr(&r->top));
    str_addf(&key, "shared.%s", name);
    cfg[2] = str_cstr(&file);
    cfg[4] = str_cstr(&key);
    (void)git_run(cfg, PROC_STDERR_NULL, NULL);
    if (gitshared_new != 0) {
        (void)sys_remove(str_cstr(&file));
    }
    str_reset(&key);
    str_addf(&key, "shared.%s.pending", id);
    pend[2] = str_cstr(&key);
    (void)git_run(pend, PROC_STDERR_NULL, NULL);
    str_free(&key);
    str_free(&file);
}

/*
 * B-44: link scrive l'ultima versione della catena, non il file che si
 * vede nella sorgente. 1 se sono diversi: il file committato nella
 * sorgente non e' la versione, o ha modifiche non committate.
 */
static int source_differs(const Source_t *s, const Oid_t *blob)
{
    const char *rp[] = { "-C", NULL, "rev-parse", "-q", "--verify", NULL, NULL };
    const char *df[] = { "-C", NULL, "--literal-pathspecs", "diff", "--quiet",
                         "HEAD", "--", NULL, NULL };
    Str_t spec = STR_INIT;
    Str_t out = STR_INIT;
    Oid_t head;
    int code = 0;
    int differs = 0;

    str_addf(&spec, "HEAD:%s", str_cstr(&s->rel));
    rp[1] = str_cstr(&s->top);
    rp[5] = str_cstr(&spec);
    if (git_capture(rp, PROC_STDERR_NULL | PROC_CLEAN_ENV, &out, &code) != GS_OK) {
        code = -1;
    }
    str_trim_eol(&out);
    if ((code != 0) || !oid_from_hex(&head, out.buf, out.len) || !oid_eq(&head, blob)) {
        differs = 1;
    } else {
        df[1] = str_cstr(&s->top);
        df[7] = str_cstr(&s->rel);
        differs = (git_capture(df, PROC_STDERR_NULL | PROC_CLEAN_ENV, &out, &code) == GS_OK) &&
                  (code == 1);
    }
    str_free(&out);
    str_free(&spec);
    return differs;
}

/* 1 se la sorgente ha un'operazione in attesa per l'ID (set o link). */
static int source_pending(const Source_t *s, const char *id)
{
    const char *args[] = { "-C", NULL, "config", "--local", "--get", NULL, NULL };
    Str_t key = STR_INIT;
    Str_t out = STR_INIT;
    int code = 1;

    str_addf(&key, "shared.%s.pending", id);
    args[1] = str_cstr(&s->top);
    args[5] = str_cstr(&key);
    if (git_capture(args, PROC_STDERR_NULL | PROC_CLEAN_ENV, &out, &code) != GS_OK) {
        code = 1;
    }
    str_free(&out);
    str_free(&key);
    return code == 0;
}

/*
 * Sorgente con un set o un link in attesa (D-100): di solito committato,
 * ma nessun comando git shared vi e' girato dopo (hook saltato). Lo
 * completa la sorgente stessa, con le sue regole (D-077), senza scartare
 * nulla. 1 se adesso non e' piu' in attesa.
 */
static int source_settle(const Source_t *s, const char *id)
{
    const char *args[] = { "-C", NULL, "shared", "hook", "complete", NULL };

    if (!source_pending(s, id)) {
        return 1;
    }
    args[1] = str_cstr(&s->top);
    (void)git_run(args, PROC_CLEAN_ENV, NULL); /* i suoi avvisi su stderr */
    return !source_pending(s, id);
}

/*
 * --adopt (D-064): la vecchia identita' non deve avere attese; se ha dati
 * locali, HEAD deve dichiararla, perche' il suo unlink si confermi con un
 * commit che discende da HEAD (D-081). GS_EXIT_OK o il codice di uscita.
 */
static int prepare_adopt(GitBatch_t *b, const SharedEntry_t *old, Adopt_t *a)
{
    Oid_t head;
    int declared = 0;

    if (old->pending != SHARED_PENDING_NONE) {
        msg_error(MSG_ERR_PENDING_OP, str_cstr(&old->path));
        return GS_EXIT_ERROR;
    }
    if (!old->valid) {
        msg_error(MSG_ERR_ALREADY_SHARED, str_cstr(&old->path),
                  str_cstr(&old->name));
        return GS_EXIT_ERROR;
    }
    str_add(&a->name, str_cstr(&old->name));
    memcpy(a->id, old->id, sizeof a->id);
    a->linked = old->in_config;
    if (a->linked) {
        if ((git_batch_query(b, "HEAD", &head) != GIT_OBJ_COMMIT) ||
            (repo_rev_declares(b, "HEAD", a->id, &declared) != GS_OK) ||
            (declared == 0)) {
            msg_error(MSG_ERR_UNLINK_UNCOMMITTED, str_cstr(&old->path));
            return GS_EXIT_ERROR;
        }
        oid_to_hex(&head, a->head);
    }
    return GS_EXIT_OK;
}

/*
 * Destinazione che esiste (D-062): un file tracciato e regolare, con una
 * base nella catena appena ricevuta (refs/shared-incoming/<id>/0). Il
 * contenuto nell'index e' quello che il commit di conferma portera': se e'
 * una versione la base c'e'; se no la si cerca nella storia del file.
 * Senza base: errore, salvo --merge (*no_base = 1, D-102). GS_EXIT_OK o il
 * codice di uscita (messaggio stampato).
 */
static int check_existing(const Repo_t *r, GitBatch_t *b, const char *id,
                          const char *dest, int merge, int *no_base)
{
    OidList_t chain = OIDLIST_INIT;
    Str_t full = STR_INIT;
    Str_t name = STR_INIT;
    Oid_t local;
    size_t base = 0U;
    int found = 0;
    int rc = GS_EXIT_ERROR;

    *no_base = 0;
    if (cmd_resolve_tracked(str_cstr(&r->top), dest, &full) != GS_OK) {
        goto exit; /* cartella, non tracciato, in conflitto, non regolare */
    }
    str_addf(&name, ":%s", dest);
    if (!git_batch_blob(b, str_cstr(&name), &local)) {
        msg_error(MSG_ERR_NOT_TRACKED, dest);
        goto exit;
    }
    peers_incoming_ref(&name, id, 0U);
    if ((chain_blobs_ref(b, str_cstr(&name), &chain) != GS_OK) ||
        (chain.n == 0U)) {
        msg_error(MSG_ERR_CHAIN, dest);
        goto exit;
    }
    found = oidlist_has(&chain, &local);
    if ((found == 0) &&
        (state_find_base(r, dest, NULL, &chain, &base, &found) != GS_OK)) {
        msg_fatal(MSG_ERR_PROC, "git log");
        rc = GS_EXIT_FATAL;
        goto exit;
    }
    if (found != 0) {
        rc = GS_EXIT_OK;
    } else if (merge != 0) {
        *no_base = 1;
        rc = GS_EXIT_OK;
    } else {
        msg_error(MSG_ERR_LINK_NO_BASE, dest);
    }

exit:
    oidlist_free(&chain);
    str_free(&name);
    str_free(&full);
    return rc;
}

int cmd_link(int argc, char **argv)
{
    CmdOpts_t o;
    Repo_t repo = REPO_INIT;
    GitBatch_t batch;
    Source_t src = { STR_INIT, STR_INIT, STR_INIT, REPO_INIT };
    Adopt_t old = { STR_INIT, "", "", 0 };
    PeerTally_t tally = { 0U, 0U, 0U };
    const SharedEntry_t *se;
    SharedEntry_t *e;
    SharedEntry_t *other;
    Str_t dest = STR_INIT;
    Str_t name = STR_INIT;
    Str_t id = STR_INIT;
    Str_t peers = STR_INIT;
    Str_t text = STR_INIT;
    Str_t paths = STR_INIT;
    Oid_t blob;
    Oid_t head_blob;
    uint8_t fetched = 0U;
    int first;
    int npos;
    int remote = 0;   /* forma remota: <url> <id> [<dest>] (D-106) */
    int relink = 0;
    int adopt = 0;
    int existing = 0; /* la destinazione c'e' gia' (D-062) */
    int no_base = 0;  /* --merge senza base (D-102) */
    int write = 1;    /* il file si scrive dall'ultima versione */
    int dest_new = 1; /* il file non e' in HEAD: entra nel commit */
    int opened = 0;
    int hooks = 0;
    int undo = 0;     /* C-21: sezione e attesa da togliere se fallisce */
    int gs_new = 0;   /* .gitshared non c'era */
    int rc = GS_EXIT_USAGE;

    first = cmd_parse(argc, argv,
                      CMD_OPT_COMMIT | CMD_OPT_FORCE | CMD_OPT_MERGE | CMD_OPT_ADOPT,
                      MSG_USAGE_LINK, &o);
    if (first < 0) {
        goto exit;
    }
    npos = argc - first;
    remote = (npos >= 2) && shared_id_is_valid(argv[first + 1]);
    if ((npos < 1) || (npos > (remote ? 3 : 2))) {
        msg_error((npos < 1) ? MSG_ERR_MISSING_PATH : MSG_ERR_TOO_MANY_ARGS);
        fputs(msg(MSG_USAGE_LINK), stderr);
        goto exit;
    }
    if ((o.force != 0U) && (o.merge != 0U)) {
        msg_error(MSG_ERR_FORCE_MERGE);
        fputs(msg(MSG_USAGE_LINK), stderr);
        goto exit;
    }
    rc = cmd_open(&repo, &batch);
    if (rc != GS_EXIT_OK) {
        goto exit;
    }
    opened = 1;
    rc = GS_EXIT_ERROR;

    /* --- sorgente ----------------------------------------------------- */
    if (remote) {
        /* Un file gia' collegato non si tocca: il fetch sovrascriverebbe
         * la ref temporanea di un link in attesa. */
        e = repo_find_id(&repo, argv[first + 1]);
        if ((e != NULL) && e->in_config) {
            msg_error(MSG_ERR_ALREADY_LINKED,
                      e->in_gitshared ? str_cstr(&e->name) : argv[first + 1],
                      argv[first + 1]);
            goto exit;
        }
        if (remote_source(&repo, &batch, argv[first], argv[first + 1], &src, &name) !=
            GS_EXIT_OK) {
            goto exit;
        }
        str_add(&id, argv[first + 1]);
    } else {
        se = find_source(&repo, argv[first], &src);
        if (se == NULL) {
            goto exit;
        }
        str_add(&name, str_cstr(&se->name));
        str_add(&id, se->id);
    }

    /* --- destinazione (D-028) ----------------------------------------- */
    if (npos == (remote ? 3 : 2)) {
        if (!cmd_repo_path(&repo, argv[argc - 1], &dest) ||
            (strchr(argv[argc - 1], '\n') != NULL)) {
            msg_error(MSG_ERR_DEST_PATH, argv[argc - 1]);
            goto exit;
        }
    } else if (remote) {
        /* il path nel creatore non si conosce: il nome del file, dalla
         * radice come in locale, se e' un nome semplice (S-08) */
        if (!name_is_simple(str_cstr(&name)) ||
            !path_join_norm("", str_cstr(&name), &dest)) {
            msg_error(MSG_ERR_LINK_NAME_UNSAFE, str_cstr(&name), argv[first],
                      argv[first + 1]);
            goto exit;
        }
    } else {
        str_add(&dest, str_cstr(&src.rel));
    }
    if (shared_path_reserved(str_cstr(&dest))) {
        msg_error(MSG_ERR_PATH_RESERVED, str_cstr(&dest)); /* B-36 */
        goto exit;
    }
    e = repo_find_id(&repo, str_cstr(&id));
    relink = (e != NULL) && e->in_gitshared && e->valid && !e->in_config;
    if (relink) {
        /* Ri-aggancio: vale la sezione che c'e' (nome e path locali). Il
         * file non si tocca mai: -f, --merge e --adopt non c'entrano. */
        if ((npos == (remote ? 3 : 2)) && !str_eq(&e->path, str_cstr(&dest))) {
            msg_error(MSG_ERR_RELINK_PATH, str_cstr(&e->name),
                      str_cstr(&e->path));
            goto exit;
        }
        if ((o.force != 0U) || (o.merge != 0U) || (o.adopt != 0U)) {
            msg_error(MSG_ERR_RELINK_OPTS, str_cstr(&e->path));
            goto exit;
        }
        str_reset(&dest);
        str_add(&dest, str_cstr(&e->path));
        str_reset(&name);
        str_add(&name, str_cstr(&e->name));
        write = 0;
    } else if ((e != NULL) && (e->in_gitshared || e->in_config)) {
        msg_error(MSG_ERR_ALREADY_LINKED, str_cstr(&name), str_cstr(&id));
        goto exit;
    } else {
        /* Destinazione gia' shared con un altro ID (D-064). */
        e = repo_find_path(&repo, str_cstr(&dest));
        if ((e != NULL) && (o.adopt == 0U)) {
            msg_error(MSG_ERR_DEST_SHARED, str_cstr(&dest), str_cstr(&e->name));
            goto exit;
        }
        if ((e == NULL) && (o.adopt != 0U)) {
            msg_error(MSG_ERR_ADOPT_NOT_SHARED, str_cstr(&dest));
            goto exit;
        }
        if (e != NULL) {
            rc = prepare_adopt(&batch, e, &old);
            if (rc != GS_EXIT_OK) {
                goto exit;
            }
            rc = GS_EXIT_ERROR;
            adopt = 1;
        }
        /* Il nome della vecchia identita' si libera con --adopt. */
        other = repo_find_name(&repo, str_cstr(&name));
        if ((other != NULL) && !(adopt && str_eq(&old.name, str_cstr(&name)))) {
            msg_error(MSG_ERR_NAME_USED, str_cstr(&name));
            goto exit;
        }
        existing = cmd_path_exists(&repo, &batch, str_cstr(&dest));
        str_reset(&text);
        str_addf(&text, "%s/%s", str_cstr(&repo.top), str_cstr(&dest));
        if (existing && sys_is_dir(str_cstr(&text))) {
            msg_error(MSG_ERR_IS_DIR, str_cstr(&dest));
            goto exit;
        }
        if (parent_is_file(&repo, &batch, str_cstr(&dest), &text)) {
            msg_error(MSG_ERR_DEST_PARENT_FILE, str_cstr(&dest), str_cstr(&text));
            goto exit;
        }
        write = !existing || (o.force != 0U);
    }

    /* --- sorgente in attesa: la si completa (D-100) ------------------- */
    if (!remote && !source_settle(&src, str_cstr(&id))) {
        msg_error(MSG_ERR_SRC_PENDING, argv[first], str_cstr(&src.top));
        goto exit;
    }
    /* B-38: il gruppo (la sorgente e i suoi peer) diventa l'elenco dei peer
     * di questa repo, e questa repo entra in quello di ognuno: al piu'
     * GS_PEER_MAX. Si controlla qui, prima di scrivere qualcosa. */
    if (!remote && (peers_of(str_cstr(&src.top), str_cstr(&id), &peers) == GS_OK) &&
        ((str_zcount(&peers) + 1U) > GS_PEER_MAX)) {
        msg_error(MSG_ERR_TOO_MANY_PEERS, str_cstr(&dest), GS_PEER_MAX);
        goto exit;
    }
    str_reset(&peers);

    /* --- catena: fetch in refs/shared-incoming/<id>/0 (D-040); in
     * remoto e' gia' arrivata (remote_source) ---------------------------- */
    if (remote) {
        fetched = 1U;
    } else {
        str_addz(&peers, str_cstr(&src.top));
        peers_fetch(str_cstr(&dest), str_cstr(&id), &peers, &fetched, &tally);
    }
    peers_incoming_ref(&text, str_cstr(&id), 0U);
    str_add(&text, ":content");
    if ((fetched == 0U) || (git_batch_blob(&batch, str_cstr(&text), &blob) == 0)) {
        msg_error(MSG_ERR_SRC_NO_VERSIONS, argv[first], str_cstr(&src.top));
        goto exit;
    }
    if (!remote && write && source_differs(&src, &blob)) {
        msg_warning(MSG_WARN_LINK_SOURCE_DIFFERS, argv[first]); /* B-44 */
    }

    /* --- destinazione che esiste: serve una base (D-062, D-029) -------- */
    if (existing && (write == 0)) {
        rc = check_existing(&repo, &batch, str_cstr(&id), str_cstr(&dest),
                            (int)o.merge, &no_base);
        if (rc != GS_EXIT_OK) {
            goto exit;
        }
        rc = GS_EXIT_ERROR;
    }
    str_reset(&text);
    str_addf(&text, "HEAD:%s", str_cstr(&dest));
    dest_new = !git_batch_blob(&batch, str_cstr(&text), &head_blob);

    /* --- scrittura: attesa per prima (come set) ------------------------- */
    if (cmd_set_pending(&repo, str_cstr(&id), "link", str_cstr(&dest),
                        str_cstr(&src.top), remote, NULL) != GS_OK) {
        msg_error(MSG_ERR_PENDING);
        goto exit;
    }
    if (adopt) {
        /* La vecchia identita' esce con lo stesso commit (D-064). */
        if (old.linked &&
            (cmd_set_pending(&repo, old.id, "unlink", str_cstr(&dest), NULL, 0,
                             old.head) != GS_OK)) {
            msg_error(MSG_ERR_PENDING);
            goto exit;
        }
        if (cmd_drop_gitshared(&repo, str_cstr(&old.name)) != GS_OK) {
            msg_error(MSG_ERR_WRITE_GITSHARED);
            goto exit;
        }
    }
    str_reset(&text);
    str_addf(&text, "%s/.gitshared", str_cstr(&repo.top));
    gs_new = !file_exists(str_cstr(&text));
    if (!relink && (cmd_write_gitshared(&repo, str_cstr(&name), str_cstr(&id),
                                        str_cstr(&dest)) != GS_OK)) {
        msg_error(MSG_ERR_WRITE_GITSHARED);
        goto exit;
    }
    undo = !relink && !adopt; /* C-21: da qui un errore toglie sezione e attesa */
    if (write && (cmd_write_file(&repo, str_cstr(&dest), &blob) != GS_OK)) {
        msg_error(MSG_ERR_WRITE_FILE, str_cstr(&dest));
        goto exit;
    }
    /* Ri-aggancio: il file non si tocca. Se non e' in HEAD (attesa
     * scartata da uno stash, poi stash pop) torna in staging con
     * .gitshared; se c'e', eventuali modifiche dell'utente restano fuori
     * dal commit. Una destinazione che esiste e' gia' nell'index
     * (check_existing): quello che c'e' in staging resta com'e'. */
    if (relink && dest_new && (cmd_stage(&repo, str_cstr(&dest)) != GS_OK)) {
        msg_error(MSG_ERR_WRITE_FILE, str_cstr(&dest));
        goto exit;
    }
    if (cmd_stage(&repo, ".gitshared") != GS_OK) {
        msg_error(MSG_ERR_STAGE_GITSHARED);
        goto exit;
    }
    undo = 0;
    hooks = 1; /* REQ-F-016: installati alla fine, vedi exit */

    /* --- la repo in memoria, per gli hook ------------------------------ */
    if (adopt) {
        e = repo_find_id(&repo, old.id);
        if (e != NULL) {
            e->in_gitshared = 0U;
            if (old.linked) {
                str_add(&e->pending_path, str_cstr(&dest));
                str_add(&e->pending_branch, str_cstr(&repo.branch));
                str_add(&e->pending_commit, old.head);
                e->pending = SHARED_PENDING_UNLINK;
            }
        }
    }
    if (!relink) {
        e = repo_add(&repo);
        str_add(&e->name, str_cstr(&name));
        str_add(&e->path, str_cstr(&dest));
        memcpy(e->id, str_cstr(&id), GS_ID_HEX_LEN + 1U);
        e->in_gitshared = 1U;
        e->valid = 1U;
    } else {
        e = repo_find_id(&repo, str_cstr(&id));
    }
    /* Come la chiave appena scritta (D-113): un ri-aggancio puo' avere
     * campi letti da chiavi vecchie rimaste. */
    str_reset(&e->pending_src);
    str_reset(&e->pending_path);
    str_reset(&e->pending_branch);
    str_reset(&e->pending_commit);
    str_add(&e->pending_src, str_cstr(&src.top));
    str_add(&e->pending_path, str_cstr(&dest));
    str_add(&e->pending_branch, str_cstr(&repo.branch));
    e->in_config = 1U;
    e->pending = SHARED_PENDING_LINK;
    e->pending_packed = 1U;
    e->pending_remote = (remote != 0) ? 1U : 0U;

    /* Ri-aggancio con la sezione gia' committata (es. un clone): niente
     * da committare, il collegamento vale subito. */
    if (relink) {
        if (cmd_reopen(&repo, &batch) != GS_EXIT_OK) {
            opened = 0;
            goto exit;
        }
        e = repo_find_id(&repo, str_cstr(&id));
        if ((e != NULL) && (e->pending == SHARED_PENDING_NONE)) {
            printf(msg(MSG_LINK_DONE), str_cstr(&dest), str_cstr(&name),
                   str_cstr(&src.top));
            putchar('\n');
            rc = GS_EXIT_OK;
            goto exit;
        }
    }

    if (o.commit == 0U) {
        printf(msg(MSG_LINK_STAGED), str_cstr(&dest), str_cstr(&name),
               str_cstr(&src.top));
        putchar('\n');
        rc = GS_EXIT_OK;
        goto exit; /* note sotto, in exit */
    }

    /* --- commit dedicato: .gitshared e il file (D-007) ------------------ */
    str_reset(&text);
    if (o.message == NULL) {
        str_addf(&text, msg(MSG_LINK_COMMIT_MSG), str_cstr(&dest));
        o.message = str_cstr(&text);
    }
    str_addz(&paths, ".gitshared");
    if (dest_new || write) {
        str_addz(&paths, str_cstr(&dest));
    }
    if (cmd_commit(&repo, o.message, &paths) != GS_OK) {
        msg_error(MSG_ERR_COMMIT);
        goto exit;
    }
    /* Completamento con un cat-file che veda il nuovo HEAD. */
    if (cmd_reopen(&repo, &batch) != GS_EXIT_OK) {
        opened = 0;
        goto exit;
    }
    e = repo_find_path(&repo, str_cstr(&dest));
    if ((e != NULL) && (e->pending == SHARED_PENDING_NONE)) {
        printf(msg(MSG_LINK_DONE), str_cstr(&dest), str_cstr(&name),
               str_cstr(&src.top));
        putchar('\n');
    }
    rc = GS_EXIT_OK;

exit:
    if ((rc != GS_EXIT_OK) && (undo != 0)) {
        undo_link(&repo, str_cstr(&id), str_cstr(&name), gs_new);
    }
    /* Che cosa succede al file, dopo il messaggio del collegamento. */
    if ((rc == GS_EXIT_OK) && adopt) {
        printf(msg(MSG_LINK_ADOPTED), str_cstr(&dest), old.id);
        putchar('\n');
    }
    if ((rc == GS_EXIT_OK) && existing && (write == 0)) {
        printf(msg(no_base ? MSG_LINK_NO_BASE_NEXT : MSG_LINK_KEPT),
               str_cstr(&dest));
        putchar('\n');
    }
    /* Hook alla fine, quando l'attesa e' aperta o gia' chiusa: il
     * post-commit serve solo se resta aperta (D-100). Un errore e' solo un
     * avviso. */
    if ((opened != 0) && (hooks != 0)) {
        (void)hooks_sync(&repo);
    }
    if (opened != 0) {
        git_batch_close(&batch);
        repo_close(&repo);
    }
    source_free(&src);
    str_free(&old.name);
    str_free(&paths);
    str_free(&text);
    str_free(&peers);
    str_free(&id);
    str_free(&name);
    str_free(&dest);
    return rc;
}
