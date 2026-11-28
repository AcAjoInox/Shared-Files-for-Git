/*
 * cmd_log.c - "git shared log" (REQ-F-032, D-120).
 *
 * Le versioni della catena di un file, dalla piu' recente: chi l'ha
 * pubblicata (l'autore dichiarato, user.name: senza firme non e'
 * verificato, R-06), quando, le unioni, i cambi di creatore, di autorita'
 * e di eredi rispetto al primo genitore. Tre segni: l'ultima versione
 * (latest), l'ultima vista sull'autorita' (authority) e la base di questo
 * branch (this branch; "changed" se il file qui e' diverso).
 *
 * Il grafo lo disegna git log --graph sulla catena; ogni riga di versione
 * si riscrive con le informazioni di git shared. Con --porcelain le stesse
 * informazioni per un programma, senza grafo (D-119): una riga
 * "<tipo> <commit> <campi>" ciascuna.
 *
 * Come status, prima si riceve (D-099): il log mostra la catena che status
 * usa. Nessuna rete.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "auth.h"
#include "chain.h"
#include "cmd.h"
#include "cmd_common.h"
#include "err.h"
#include "git.h"
#include "mem.h"
#include "msg.h"
#include "receive.h"
#include "shared.h"
#include "state.h"
#include "sys.h"

/* Cosa sa il log di un file, oltre alle righe di git log. */
typedef struct {
    const SharedEntry_t *e;
    Oid_t tip;          /* refs/shared/<id> */
    Oid_t auth_tip;     /* refs/shared-authority/<id>; len = 0 se non c'e' */
    Oid_t base;         /* commit della base di questo branch; len = 0 */
    int base_changed;   /* 1 se il file in HEAD e' diverso dalla base */
    int porcelain;
    int eol;
} LogCtx_t;

/* Base del branch (state_find_base): la versione da cui parte il file in
 * HEAD. Solo per un file dichiarato su questo branch e presente in HEAD. */
static eGsErr_t find_base(const Repo_t *r, GitBatch_t *b, LogCtx_t *c)
{
    const SharedEntry_t *e = c->e;
    OidList_t blobs = OIDLIST_INIT;
    OidList_t commits = OIDLIST_INIT;
    Str_t name = STR_INIT;
    Oid_t h;
    size_t base = 0U;
    int found = 0;
    eGsErr_t err = GS_OK;

    c->base.len = 0U;
    c->base_changed = 0;
    if (!e->in_gitshared || !e->valid) {
        goto exit;
    }
    str_addf(&name, "HEAD:%s", str_cstr(&e->path));
    if (git_batch_blob(b, str_cstr(&name), &h) == 0) {
        goto exit; /* file non in HEAD: nessuna base */
    }
    chain_ref(&name, e->id);
    err = chain_versions_ref(b, str_cstr(&name), &blobs, &commits);
    if ((err == GS_OK) && (blobs.n > 0U)) {
        err = state_find_base(r, str_cstr(&e->path), &e->old_paths, &blobs, &base,
                              &found);
    }
    if ((err == GS_OK) && (found != 0)) {
        c->base = commits.v[base];
        c->base_changed = !oid_eq(&h, &blobs.v[base]);
    }

exit:
    oidlist_free(&blobs);
    oidlist_free(&commits);
    str_free(&name);
    return err;
}

/* Voci della versione v e del suo primo genitore p (p NULL: prima
 * versione). Cambi di creatore, di autorita' (crown, crown --restore) e
 * di eredi. Voci non valide: nessun cambio da dire. */
typedef struct {
    Str_t creator;   /* URL se il creatore e' cambiato (o e' comparso) */
    Str_t authority; /* URL se l'autorita' e' cambiata, creatore uguale */
    int heirs;       /* 1 se la voce heirs e' cambiata */
    int same;        /* 1 se il contenuto e' quello del primo genitore */
} Changes_t;

static void changes_of(GitBatch_t *b, const Oid_t *v, const Oid_t *p, Changes_t *ch)
{
    AuthBlobs_t vb;
    AuthBlobs_t pb;
    Auth_t va = AUTH_INIT;
    Auth_t pa = AUTH_INIT;
    Str_t name = STR_INIT;
    char hex[OID_HEX_SIZE];
    Oid_t vc;
    Oid_t pc;

    str_reset(&ch->creator);
    str_reset(&ch->authority);
    ch->heirs = 0;
    ch->same = 0;
    auth_blobs(b, v, &vb);
    if (p != NULL) {
        auth_blobs(b, p, &pb);
        oid_to_hex(v, hex);
        str_addf(&name, "%s:content", hex);
        vc.len = 0U;
        pc.len = 0U;
        (void)git_batch_blob(b, str_cstr(&name), &vc);
        oid_to_hex(p, hex);
        str_reset(&name);
        str_addf(&name, "%s:content", hex);
        (void)git_batch_blob(b, str_cstr(&name), &pc);
        ch->same = (vc.len > 0U) && oid_eq(&vc, &pc);
    } else {
        pb.creator.len = 0U;
        pb.sovereigns.len = 0U;
        pb.heirs.len = 0U;
    }
    ch->heirs = (vb.heirs.len != pb.heirs.len) ||
                ((vb.heirs.len > 0U) && !oid_eq(&vb.heirs, &pb.heirs));
    if (!auth_blobs_eq(&vb, &pb) && (auth_read(b, &vb, &va) == GS_OK) &&
        (auth_read(b, &pb, &pa) == GS_OK)) {
        if (!auth_creator_same(&va, &pa)) {
            str_add(&ch->creator, str_cstr(&va.creator));
        } else if (!auth_same(&va, &pa)) {
            str_add(&ch->authority, auth_authority(&va));
        } else {
            /* stessa autorita' in forma normale */
        }
    }
    auth_free(&va);
    auth_free(&pa);
    str_free(&name);
}

/* Una versione: i campi di una riga di git log (fmt in cmd_log). */
typedef struct {
    const char *graph;   /* il grafo davanti alla riga ("" con --porcelain) */
    Oid_t commit;
    const char *abbrev;  /* oid abbreviato da git */
    unsigned long at;    /* data dell'autore */
    const char *parents; /* oid dei genitori separati da spazi */
    const char *author;
} LogLine_t;

/* Separatore dei campi nel formato di git log: non compare nei nomi. */
#define LOG_SEP '\x01'

/* Divide una riga di git log nei campi; 0 se non e' una riga di versione
 * (solo grafo: viene stampata com'e'). */
static int parse_line(char *line, LogLine_t *l)
{
    char *f[6];
    size_t n = 0U;
    char *p = strchr(line, LOG_SEP);

    if (p == NULL) {
        return 0;
    }
    *p = '\0';
    f[n++] = line;
    while ((p != NULL) && (n < 6U)) {
        f[n++] = p + 1;
        p = strchr(p + 1, LOG_SEP);
        if (p != NULL) {
            *p = '\0';
        }
    }
    if ((n < 6U) || !oid_from_hex(&l->commit, f[1], strlen(f[1]))) {
        return 0;
    }
    l->graph = f[0];
    l->abbrev = f[2];
    l->at = strtoul(f[3], NULL, 10);
    l->parents = f[4];
    l->author = f[5];
    return 1;
}

/* Primo genitore di una riga: 1 se c'e'. */
static int first_parent(const LogLine_t *l, Oid_t *p)
{
    const char *sp = strchr(l->parents, ' ');
    size_t len = (sp != NULL) ? (size_t)(sp - l->parents) : strlen(l->parents);

    return (len > 0U) && oid_from_hex(p, l->parents, len);
}

static void print_porcelain(const LogCtx_t *c, GitBatch_t *b, const LogLine_t *l,
                            const Changes_t *ch)
{
    char hex[OID_HEX_SIZE];
    char blob[OID_HEX_SIZE];
    Str_t name = STR_INIT;
    const char *p = l->parents;
    const char *sp;
    Oid_t v;

    oid_to_hex(&l->commit, hex);
    str_addf(&name, "%s:content", hex);
    v.len = 0U;
    (void)git_batch_blob(b, str_cstr(&name), &v);
    oid_to_hex(&v, blob);
    cmd_porc(c->eol, "version", hex, "%s %lu %s", (v.len > 0U) ? blob : "-", l->at,
             l->author);
    while (*p != '\0') {
        sp = strchr(p, ' ');
        if (sp == NULL) {
            cmd_porc(c->eol, "parent", hex, "%s", p);
            break;
        }
        cmd_porc(c->eol, "parent", hex, "%.*s", (int)(sp - p), p);
        p = sp + 1;
    }
    if (oid_eq(&l->commit, &c->tip)) {
        cmd_porc(c->eol, "latest", hex, NULL);
    }
    if ((c->auth_tip.len > 0U) && oid_eq(&l->commit, &c->auth_tip)) {
        cmd_porc(c->eol, "authority-tip", hex, NULL);
    }
    if ((c->base.len > 0U) && oid_eq(&l->commit, &c->base)) {
        cmd_porc(c->eol, "base", hex, "%s", (c->base_changed != 0) ? "changed" : "same");
    }
    if (ch->creator.len > 0U) {
        cmd_porc(c->eol, "creator", hex, "%s", str_cstr(&ch->creator));
    }
    if (ch->authority.len > 0U) {
        cmd_porc(c->eol, "authority", hex, "%s", str_cstr(&ch->authority));
    }
    if (ch->heirs != 0) {
        cmd_porc(c->eol, "heirs", hex, NULL);
    }
    str_free(&name);
}

/* Aggiunge a out un pezzo, con il separatore se non e' il primo. */
static void add_part(Str_t *out, const char *sep, const char *part)
{
    if (out->len > 0U) {
        str_add(out, sep);
    }
    str_add(out, part);
}

static void print_human(const LogCtx_t *c, const LogLine_t *l, const Changes_t *ch,
                        int root)
{
    Str_t marks = STR_INIT;
    Str_t events = STR_INIT;
    Str_t tmp = STR_INIT;
    char date[32];

    if (oid_eq(&l->commit, &c->tip)) {
        add_part(&marks, ", ", msg(MSG_LOG_LATEST));
    }
    if ((c->auth_tip.len > 0U) && oid_eq(&l->commit, &c->auth_tip)) {
        add_part(&marks, ", ", msg(MSG_LOG_AUTHORITY));
    }
    if ((c->base.len > 0U) && oid_eq(&l->commit, &c->base)) {
        add_part(&marks, ", ",
                 msg((c->base_changed != 0) ? MSG_LOG_HERE_CHANGED : MSG_LOG_HERE));
    }
    if (root != 0) {
        add_part(&events, "; ", msg(MSG_LOG_FIRST));
    }
    if (strchr(l->parents, ' ') != NULL) {
        add_part(&events, "; ", msg(MSG_LOG_MERGE));
    }
    if (ch->same != 0) {
        add_part(&events, "; ", msg(MSG_LOG_SAME));
    }
    if (ch->creator.len > 0U) {
        str_addf(&tmp, msg(MSG_LOG_CREATOR), str_cstr(&ch->creator));
        add_part(&events, "; ", str_cstr(&tmp));
    }
    if (ch->authority.len > 0U) {
        str_reset(&tmp);
        str_addf(&tmp, msg(MSG_LOG_AUTH), str_cstr(&ch->authority));
        add_part(&events, "; ", str_cstr(&tmp));
    }
    if (ch->heirs != 0) {
        add_part(&events, "; ", msg(MSG_LOG_HEIRS));
    }
    printf("%s%s %s %s", l->graph, l->abbrev, cmd_local_date(l->at, date, sizeof date),
           l->author);
    if (marks.len > 0U) {
        printf(" (%s)", str_cstr(&marks));
    }
    if (events.len > 0U) {
        printf(": %s", str_cstr(&events));
    }
    putchar('\n');
    str_free(&marks);
    str_free(&events);
    str_free(&tmp);
}

/*
 * git log sulla catena: una riga per versione con i campi separati da
 * LOG_SEP, il grafo davanti (solo per chi legge). git log e' porcelain:
 * le opzioni che la configurazione dell'utente puo' cambiare sono fissate
 * (come in state.c, D-094): --no-color, --no-show-signature, --topo-order;
 * il formato e' esplicito, quindi format.pretty e log.decorate non
 * contano.
 */
static int run_log(const LogCtx_t *c, GitBatch_t *b, const char *ref,
                   const char *count)
{
    const char *args[12];
    size_t na = 0U;
    Proc_t p;
    Str_t line = STR_INIT;
    Str_t author = STR_INIT;
    Changes_t ch = { STR_INIT, STR_INIT, 0, 0 };
    LogLine_t l;
    Oid_t parent;
    int code = 0;
    int root;
    int rc = GS_EXIT_OK;

    args[na++] = "log";
    args[na++] = "--no-color";
    args[na++] = "--no-show-signature";
    args[na++] = "--topo-order";
    args[na++] = "--format=%x01%H%x01%h%x01%at%x01%P%x01%an";
    if (c->porcelain == 0) {
        args[na++] = "--graph";
    }
    if (count != NULL) {
        args[na++] = "-n";
        args[na++] = count;
    }
    args[na++] = ref;
    args[na++] = "--";
    args[na] = NULL;
    if (git_start(&p, args, 0U) != GS_OK) {
        msg_fatal(MSG_ERR_PROC, "git log");
        return GS_EXIT_FATAL;
    }
    while (proc_read(&p, '\n', &line) != 0) {
        if (!parse_line(line.buf, &l)) {
            if (c->porcelain == 0) {
                printf("%s\n", str_cstr(&line)); /* solo grafo */
            }
            continue;
        }
        /* l'autore lo scrive chi crea la versione (S-07) */
        str_reset(&author);
        str_add_printable(&author, l.author);
        l.author = str_cstr(&author);
        root = !first_parent(&l, &parent);
        changes_of(b, &l.commit, (root != 0) ? NULL : &parent, &ch);
        if (c->porcelain != 0) {
            print_porcelain(c, b, &l, &ch);
        } else {
            print_human(c, &l, &ch, root);
        }
    }
    if ((proc_finish(&p, &code) != GS_OK) || (code != 0)) {
        msg_fatal(MSG_ERR_PROC, "git log");
        rc = GS_EXIT_FATAL;
    }
    str_free(&ch.creator);
    str_free(&ch.authority);
    str_free(&author);
    str_free(&line);
    return rc;
}

/* Il file da mostrare: il path (relativo alla cartella corrente) o l'id;
 * senza argomento l'unico file collegato. NULL con l'errore stampato. */
static const SharedEntry_t *pick(Repo_t *r, const char *arg)
{
    Str_t full = STR_INIT;
    SharedEntry_t *e = NULL;
    size_t i;
    size_t n = 0U;

    if (arg != NULL) {
        if (cmd_repo_path(r, arg, &full)) {
            e = repo_find_path(r, str_cstr(&full));
        }
        if (e == NULL) {
            e = repo_find_id(r, arg);
        }
        if ((e == NULL) || (e->in_gitshared && !e->valid)) {
            msg_error(MSG_ERR_NOT_SHARED_PATH, arg);
            e = NULL;
        }
    } else {
        for (i = 0U; i < r->n; i++) {
            if (r->v[i].in_config && (r->v[i].id[0] != '\0')) {
                e = &r->v[i];
                n++;
            }
        }
        if (n == 0U) {
            msg_error(MSG_ERR_LOG_NONE);
        } else if (n > 1U) {
            msg_error(MSG_ERR_LOG_WHICH);
            e = NULL;
        } else {
            /* l'unico */
        }
    }
    if ((e != NULL) && !e->in_config) {
        msg_error(MSG_ERR_NOT_LINKED, e->in_gitshared ? str_cstr(&e->path) : arg);
        e = NULL;
    }
    str_free(&full);
    return e;
}

int cmd_log(int argc, char **argv)
{
    Repo_t repo = REPO_INIT;
    GitBatch_t batch;
    Received_t rv = RECEIVED_INIT;
    LogCtx_t c;
    Str_t ref = STR_INIT;
    const char *arg = NULL;
    const char *count = NULL;
    char *end;
    int batch_open = 0;
    int end_opts = 0;
    int i;
    int rc = GS_EXIT_USAGE;
    eGsErr_t err;

    c.porcelain = 0;
    c.eol = '\n';
    for (i = 1; i < argc; i++) {
        if ((end_opts == 0) && (strcmp(argv[i], "--") == 0)) {
            end_opts = 1;
        } else if ((end_opts == 0) && ((strcmp(argv[i], "--porcelain") == 0) ||
                                       (strcmp(argv[i], "--porcelain=1") == 0))) {
            c.porcelain = 1;
        } else if ((end_opts == 0) && (strcmp(argv[i], "-z") == 0)) {
            c.porcelain = 1;
            c.eol = '\0';
        } else if ((end_opts == 0) && (strcmp(argv[i], "-n") == 0)) {
            if (((i + 1) >= argc) || (strtoul(argv[i + 1], &end, 10) == 0UL) ||
                (*end != '\0') || (argv[i + 1][0] == '-')) {
                msg_error(MSG_ERR_OPT_VALUE, argv[i]);
                goto usage;
            }
            count = argv[++i];
        } else if ((end_opts == 0) && (argv[i][0] == '-') && (argv[i][1] != '\0')) {
            msg_error(MSG_ERR_UNKNOWN_OPT, argv[i]);
            goto usage;
        } else if (arg == NULL) {
            arg = argv[i];
        } else {
            msg_error(MSG_ERR_TOO_MANY_ARGS);
            goto usage;
        }
    }

    rc = GS_EXIT_FATAL;
    err = repo_open(&repo);
    if (err != GS_OK) {
        msg_fatal((err == GS_ERR_NOT_REPO)  ? MSG_ERR_NOT_REPO
                  : (err == GS_ERR_FORMAT) ? MSG_ERR_READ_GITSHARED
                                           : MSG_ERR_PROC,
                  "git");
        goto exit;
    }
    if (git_batch_open(&batch) != GS_OK) {
        msg_fatal(MSG_ERR_PROC, "git cat-file");
        goto exit;
    }
    batch_open = 1;
    rc = GS_EXIT_OK;
    if (cmd_complete(&repo, &batch, 1) != GS_OK) {
        msg_error(MSG_ERR_PENDING);
        rc = GS_EXIT_ERROR; /* si mostra comunque il log */
    }
    c.e = pick(&repo, arg);
    if (c.e == NULL) {
        rc = GS_EXIT_ERROR;
        goto exit;
    }
    /* Prima si riceve (D-099), come status. */
    if ((c.e->pending == SHARED_PENDING_NONE) &&
        (receive_entry(&repo, c.e, &batch, 0, &rv) != GS_OK)) {
        /* catena assente: lo dice la domanda sotto */
    }
    chain_ref(&ref, c.e->id);
    if (git_batch_query(&batch, str_cstr(&ref), &c.tip) != GIT_OBJ_COMMIT) {
        msg_error(MSG_ERR_LOG_NO_CHAIN,
                  c.e->in_gitshared ? str_cstr(&c.e->path) : c.e->id);
        rc = GS_EXIT_ERROR;
        goto exit;
    }
    /* AB-08: il segno "authority" solo sulla versione vista sull'autorita'
     * di T, con la regola di status (D-112): la ricezione l'ha calcolata. */
    c.auth_tip = rv.auth_tip;
    if (find_base(&repo, &batch, &c) != GS_OK) {
        c.base.len = 0U; /* solo un segno in meno */
    }
    if (c.porcelain != 0) {
        sys_stdout_binary(); /* solo LF, anche su Windows (D-119) */
        printf("# porcelain 1%c", c.eol);
        printf("# id %s%c", c.e->id, c.eol);
        printf("# path %s%c", c.e->in_gitshared ? str_cstr(&c.e->path) : "", c.eol);
    }
    chain_ref(&ref, c.e->id);
    if (run_log(&c, &batch, str_cstr(&ref), count) != GS_EXIT_OK) {
        rc = GS_EXIT_FATAL;
    }
    goto exit;

usage:
    fputs(msg(MSG_USAGE_LOG), stderr);
    rc = GS_EXIT_USAGE;

exit:
    if (batch_open != 0) {
        git_batch_close(&batch);
    }
    received_free(&rv);
    repo_close(&repo);
    str_free(&ref);
    return rc;
}
