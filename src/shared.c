/*
 * shared.c - la repo corrente e i suoi file shared.
 */
#include "shared.h"

#include <stdlib.h>
#include <string.h>

#include "chain.h"
#include "mem.h"
#include "merge.h"
#include "msg.h"
#include "peers.h"
#include "sys.h"

/* Processi avviati da repo_open e non ancora chiusi. */
#define RUN_TOP  0x1U
#define RUN_HEAD 0x2U
#define RUN_CFG  0x4U

/* Chiave "shared.<sub>.<campo>\n<valore>" di `git config -z`. */
typedef struct {
    const char *sub;
    size_t sub_len;
    const char *field;
    const char *value;
} CfgKey_t;

/* ---- Voci ---------------------------------------------------------------- */

static void entry_init(SharedEntry_t *e)
{
    str_init(&e->name);
    str_init(&e->path);
    str_init(&e->branches);
    str_init(&e->peers);
    str_init(&e->pending_src);
    str_init(&e->pending_branch);
    str_init(&e->pending_path);
    str_init(&e->pending_commit);
    e->id[0] = '\0';
    e->in_gitshared = 0U;
    e->in_config = 0U;
    e->valid = 0U;
    e->pending = SHARED_PENDING_NONE;
}

static void entry_free(SharedEntry_t *e)
{
    str_free(&e->name);
    str_free(&e->path);
    str_free(&e->branches);
    str_free(&e->peers);
    str_free(&e->pending_src);
    str_free(&e->pending_branch);
    str_free(&e->pending_path);
    str_free(&e->pending_commit);
}

void repo_close(Repo_t *r)
{
    size_t i;

    for (i = 0U; i < r->n; i++) {
        entry_free(&r->v[i]);
    }
    free(r->v);
    r->v = NULL;
    r->n = 0U;
    r->cap = 0U;
    str_free(&r->top);
    str_free(&r->prefix);
    str_free(&r->branch);
    str_free(&r->objfmt);
    str_free(&r->self_path);
}

SharedEntry_t *repo_add(Repo_t *r)
{
    SharedEntry_t *e;

    if (r->n == r->cap) {
        r->cap = (r->cap == 0U) ? 8U : (r->cap * 2U);
        r->v = xrealloc(r->v, r->cap * sizeof *r->v);
    }
    e = &r->v[r->n];
    r->n++;
    entry_init(e);
    return e;
}

static SharedEntry_t *find_sub(Repo_t *r, const char *s, size_t len, int by_id)
{
    SharedEntry_t *found = NULL;
    const char *key;
    size_t i;

    for (i = 0U; (i < r->n) && (found == NULL); i++) {
        key = by_id ? r->v[i].id : str_cstr(&r->v[i].name);
        if ((strlen(key) == len) && (memcmp(key, s, len) == 0)) {
            found = &r->v[i];
        }
    }
    return found;
}

SharedEntry_t *repo_find_path(Repo_t *r, const char *path)
{
    SharedEntry_t *found = NULL;
    size_t i;

    for (i = 0U; (i < r->n) && (found == NULL); i++) {
        if (r->v[i].in_gitshared && str_eq(&r->v[i].path, path)) {
            found = &r->v[i];
        }
    }
    return found;
}

SharedEntry_t *repo_find_name(Repo_t *r, const char *name)
{
    return find_sub(r, name, strlen(name), 0);
}

SharedEntry_t *repo_find_id(Repo_t *r, const char *id)
{
    return find_sub(r, id, strlen(id), 1);
}

int shared_is_active(const SharedEntry_t *e, const char *branch)
{
    return (branch[0] != '\0') && str_zhas(&e->branches, branch);
}

static int id_is_valid(const char *s, size_t len)
{
    size_t i;
    int ok = (len == GS_ID_HEX_LEN);

    for (i = 0U; ok && (i < len); i++) {
        ok = ((s[i] >= '0') && (s[i] <= '9')) || ((s[i] >= 'a') && (s[i] <= 'f'));
    }
    return ok;
}

/* Path relativo (D-003): non vuoto, senza '/' o '\' iniziale ne' "C:". */
static int path_is_valid(const Str_t *p)
{
    const char *s = str_cstr(p);

    return (s[0] != '\0') && (s[0] != '/') && (s[0] != '\\') &&
           (s[1] != ':') && (p->len <= GS_PATH_MAX);
}

eGsErr_t shared_new_id(char id[GS_ID_HEX_LEN + 1U])
{
    static const char digits[] = "0123456789abcdef";
    uint8_t raw[GS_ID_RAW_LEN];
    size_t i;
    eGsErr_t err = sys_random(raw, sizeof raw);

    if (err == GS_OK) {
        raw[6] = (uint8_t)((raw[6] & 0x0FU) | 0x40U); /* versione 4 */
        raw[8] = (uint8_t)((raw[8] & 0x3FU) | 0x80U); /* variante RFC 4122 */
        for (i = 0U; i < GS_ID_RAW_LEN; i++) {
            id[2U * i] = digits[raw[i] >> 4U];
            id[(2U * i) + 1U] = digits[raw[i] & 0x0FU];
        }
        id[GS_ID_HEX_LEN] = '\0';
    }
    return err;
}

/* ---- Lettura della configurazione --------------------------------------- */

/*
 * Divide "shared.<sub>.<campo>\n<valore>". Il sub puo' contenere punti:
 * il campo e' cio' che segue l'ultimo punto della chiave (§6.3).
 */
static int split_key(const char *rec, CfgKey_t *k)
{
    static const char prefix[] = "shared.";
    const size_t plen = sizeof prefix - 1U;
    const char *nl = strchr(rec, '\n');
    const char *end = (nl != NULL) ? nl : (rec + strlen(rec));
    const char *dot = NULL;
    const char *c;
    int ok = 0;

    if ((nl != NULL) && ((size_t)(end - rec) > plen) &&
        (strncmp(rec, prefix, plen) == 0)) {
        for (c = rec + plen; c < end; c++) {
            if (*c == '.') {
                dot = c;
            }
        }
        if ((dot != NULL) && (dot > (rec + plen))) {
            k->sub = rec + plen;
            k->sub_len = (size_t)(dot - k->sub);
            k->field = dot + 1;
            k->value = nl + 1;
            ok = 1;
        }
    }
    return ok;
}

/* 1 se il campo della chiave e' `name` (il campo termina con '\n'). */
static int field_is(const CfgKey_t *k, const char *name)
{
    size_t n = strlen(name);

    return (strncmp(k->field, name, n) == 0) && (k->field[n] == '\n');
}

/* Record di .gitshared: sezione per nome. */
static void parse_gitshared_rec(Repo_t *r, const char *rec, int with_branches)
{
    CfgKey_t k;
    SharedEntry_t *e;

    if (split_key(rec, &k) != 0) {
        e = find_sub(r, k.sub, k.sub_len, 0);
        if (e == NULL) {
            e = repo_add(r);
            str_addn(&e->name, k.sub, k.sub_len);
            e->in_gitshared = 1U;
        }
        if (field_is(&k, "id")) {
            if (id_is_valid(k.value, strlen(k.value))) {
                memcpy(e->id, k.value, GS_ID_HEX_LEN + 1U);
            } else {
                e->id[0] = '\0';
            }
        } else if (field_is(&k, "path")) {
            str_reset(&e->path);
            str_add(&e->path, k.value);
        } else if (with_branches && field_is(&k, "branch")) {
            str_addz(&e->branches, k.value);
        } else {
            /* altri campi (url: fase 2) */
        }
    }
}

/* Record di .git/config (scope locale): sezione per ID. I nomi delle
 * variabili arrivano in minuscolo da git ("pendingsource"). */
static void parse_local_rec(Repo_t *r, const char *rec)
{
    static const char *const pending[] = { "", "set", "link", "unlink" };
    CfgKey_t k;
    SharedEntry_t *e;
    unsigned i;

    if ((split_key(rec, &k) != 0) && id_is_valid(k.sub, k.sub_len)) {
        e = find_sub(r, k.sub, k.sub_len, 1);
        if (e == NULL) {
            e = repo_add(r);
            memcpy(e->id, k.sub, GS_ID_HEX_LEN);
            e->id[GS_ID_HEX_LEN] = '\0';
        }
        e->in_config = 1U;
        if (field_is(&k, "pending")) {
            for (i = 1U; i < (sizeof pending / sizeof pending[0]); i++) {
                if (strcmp(k.value, pending[i]) == 0) {
                    e->pending = i & 0x3U; /* bitfield di 2 bit (C-17) */
                }
            }
        } else if (field_is(&k, "pendingsource")) {
            str_reset(&e->pending_src);
            str_add(&e->pending_src, k.value);
        } else if (field_is(&k, "pendingbranch")) {
            str_reset(&e->pending_branch);
            str_add(&e->pending_branch, k.value);
        } else if (field_is(&k, "pendingpath")) {
            str_reset(&e->pending_path);
            str_add(&e->pending_path, k.value);
        } else if (field_is(&k, "pendingcommit")) {
            str_reset(&e->pending_commit);
            str_add(&e->pending_commit, k.value);
        } else if (field_is(&k, "branch")) {
            str_addz(&e->branches, k.value);
        } else if (field_is(&k, "peer")) {
            str_addz(&e->peers, k.value);
        } else {
            /* campi sconosciuti: ignorati */
        }
    }
}

eGsErr_t repo_load_gitshared(Repo_t *r, const char *src, int is_blob,
                             int with_branches)
{
    const char *args[] = { "config", "-z", NULL, NULL, "--get-regexp",
                           "^shared\\.", NULL };
    Str_t rec = STR_INIT;
    Proc_t p;
    int code = 0;
    size_t i;
    eGsErr_t err;

    args[2] = is_blob ? "--blob" : "-f";
    args[3] = src;
    err = git_start(&p, args, is_blob ? PROC_STDERR_NULL : 0U);
    if (err != GS_OK) {
        goto exit;
    }
    while (proc_read(&p, '\0', &rec) != 0) {
        parse_gitshared_rec(r, str_cstr(&rec), with_branches);
    }
    err = proc_finish(&p, &code);
    /* 0 = trovato, 1 = nessuna chiave o file assente, altro = file non
     * valido (verificato con git 2.43). Un blob illeggibile vale vuoto. */
    if ((err == GS_OK) && (code > 1) && !is_blob) {
        err = GS_ERR_FORMAT;
    }
    for (i = 0U; i < r->n; i++) {
        if (r->v[i].in_gitshared) {
            r->v[i].valid = (r->v[i].id[0] != '\0') && path_is_valid(&r->v[i].path);
        }
    }

exit:
    str_free(&rec);
    return err;
}

/* Divide l'output di rev-parse (una riga per valore; il prefisso puo'
 * essere una riga vuota). */
static eGsErr_t parse_locate(const Str_t *out, int code, Str_t *top,
                             Str_t *prefix, Str_t *objfmt)
{
    const char *l1 = str_cstr(out);
    const char *l2;
    const char *l3;
    eGsErr_t err = GS_ERR_NOT_REPO; /* anche dentro .git: nessun worktree */

    l2 = strchr(l1, '\n');
    l3 = (l2 != NULL) ? strchr(l2 + 1, '\n') : NULL;
    if ((code == 0) && (l2 != NULL) && (l2 != l1) && (l3 != NULL)) {
        str_addn(top, l1, (size_t)(l2 - l1));
        str_addn(prefix, l2 + 1, (size_t)(l3 - (l2 + 1)));
        str_add(objfmt, l3 + 1);
        str_trim_eol(objfmt);
        err = GS_OK;
    }
    return err;
}

eGsErr_t repo_locate(const char *dir, Str_t *top, Str_t *prefix,
                     Str_t *objfmt)
{
    const char *args[] = { "-C", dir, "rev-parse", "--show-toplevel",
                           "--show-prefix", "--show-object-format", NULL };
    Str_t out = STR_INIT;
    int code = 0;
    eGsErr_t err = git_capture(args, PROC_STDERR_NULL, &out, &code);

    if (err == GS_OK) {
        err = parse_locate(&out, code, top, prefix, objfmt);
    }
    str_free(&out);
    return err;
}

eGsErr_t repo_open(Repo_t *r)
{
    static const char *const a_top[] = { "rev-parse", "--show-toplevel",
                                         "--show-prefix",
                                         "--show-object-format", NULL };
    static const char *const a_head[] = { "symbolic-ref", "-q", "HEAD", NULL };
    static const char *const a_cfg[] = { "config", "-z", "--show-scope",
                                         "--get-regexp", "^shared\\.", NULL };
    static const char heads[] = "refs/heads/";
    Proc_t p_top;
    Proc_t p_head;
    Proc_t p_cfg;
    Str_t out = STR_INIT;
    Str_t file = STR_INIT;
    const char *scope;
    const char *rec;
    const char *end;
    unsigned run = 0U;
    int code = 0;
    eGsErr_t err;

    /* Tre processi indipendenti avviati insieme: su Windows l'avvio di un
     * processo e' il costo dominante (§6.1). */
    err = git_start(&p_top, a_top, PROC_STDERR_NULL);
    if (err != GS_OK) {
        goto exit;
    }
    run |= RUN_TOP;
    err = git_start(&p_head, a_head, PROC_STDERR_NULL);
    if (err != GS_OK) {
        goto exit;
    }
    run |= RUN_HEAD;
    err = git_start(&p_cfg, a_cfg, 0U);
    if (err != GS_OK) {
        goto exit;
    }
    run |= RUN_CFG;

    run &= ~RUN_TOP;
    err = proc_collect(&p_top, &out, &code);
    if (err == GS_OK) {
        err = parse_locate(&out, code, &r->top, &r->prefix, &r->objfmt);
    }
    if (err != GS_OK) {
        goto exit;
    }

    /* .gitshared mentre gli altri due processi finiscono. */
    str_addf(&file, "%s/.gitshared", str_cstr(&r->top));
    err = repo_load_gitshared(r, str_cstr(&file), 0, 0);
    if (err != GS_OK) {
        goto exit;
    }

    run &= ~RUN_HEAD;
    err = proc_collect(&p_head, &out, &code);
    if (err != GS_OK) {
        goto exit;
    }
    str_trim_eol(&out);
    if ((code == 0) && (strncmp(str_cstr(&out), heads, sizeof heads - 1U) == 0)) {
        str_add(&r->branch, out.buf + (sizeof heads - 1U));
    }

    /* Record "scope\0chiave\nvalore\0": le sezioni per ID contano solo se
     * locali. */
    run &= ~RUN_CFG;
    err = proc_collect(&p_cfg, &out, &code);
    if (err != GS_OK) {
        goto exit;
    }
    if (code > 1) {
        err = GS_ERR_FORMAT; /* config non valido: git ha gia' detto dove */
        goto exit;
    }
    rec = str_cstr(&out);
    end = rec + out.len;
    while (rec < end) {
        scope = rec;
        rec += strlen(rec) + 1U;
        if (rec < end) {
            if ((strcmp(scope, "local") == 0) &&
                (strncmp(rec, "shared.selfpath\n", 16U) == 0)) {
                str_reset(&r->self_path);
                str_add(&r->self_path, rec + 16U);
            } else if (strcmp(scope, "local") == 0) {
                parse_local_rec(r, rec);
            } else {
                /* chiavi fuori dal livello locale: ignorate */
            }
            rec += strlen(rec) + 1U;
        }
    }

exit:
    if ((run & RUN_TOP) != 0U) {
        (void)proc_finish(&p_top, &code);
    }
    if ((run & RUN_HEAD) != 0U) {
        (void)proc_finish(&p_head, &code);
    }
    if ((run & RUN_CFG) != 0U) {
        (void)proc_finish(&p_cfg, &code);
    }
    str_free(&file);
    str_free(&out);
    return err;
}

/* ---- Operazioni in attesa (REQ-F-023) ----------------------------------- */

/* "git config <a1> <a2> [<a3>]" (a3 puo' essere NULL). */
static eGsErr_t config_run(const char *a1, const char *a2, const char *a3)
{
    const char *args[] = { "config", a1, a2, a3, NULL };

    return git_run(args, 0U, NULL);
}

/* Branch del comando in attesa (D-077); se non registrato, il corrente. */
static const Str_t *pending_branch(const Repo_t *r, const SharedEntry_t *e)
{
    return (e->pending_branch.len > 0U) ? &e->pending_branch : &r->branch;
}

/* Branch attivi: copia del default (D-057); se manca, il branch del
 * comando (D-077). */
static eGsErr_t copy_branches(const Repo_t *r, SharedEntry_t *e,
                              const SharedEntry_t *committed)
{
    const char *unset_args[] = { "config", "--unset-all", NULL, NULL };
    const Str_t *list = &committed->branches;
    const Str_t *fallback = pending_branch(r, e);
    Str_t current = STR_INIT;
    Str_t key = STR_INIT;
    Str_t ignored = STR_INIT;
    const char *br;
    int code = 0;
    eGsErr_t err;

    if (list->len == 0U) {
        if (fallback->len > 0U) {
            str_addz(&current, str_cstr(fallback));
        } else {
            msg_warning(MSG_WARN_NO_BRANCH, str_cstr(&committed->name), e->id);
        }
        list = &current;
    }
    str_addf(&key, "shared.%s.branch", e->id);
    unset_args[2] = str_cstr(&key);
    err = git_capture(unset_args, 0U, &ignored, &code); /* 5 = non c'era */
    str_reset(&e->branches);
    for (br = str_znext(list, NULL); (br != NULL) && (err == GS_OK);
         br = str_znext(list, br)) {
        err = config_run("--add", str_cstr(&key), br);
        str_addz(&e->branches, br);
    }
    str_free(&ignored);
    str_free(&key);
    str_free(&current);
    return err;
}

/* "git config --unset shared.<id>.<field>": una chiave gia' assente
 * (codice 5) non e' un errore, cosi' il completamento si puo' ripetere. */
static eGsErr_t config_unset(const char *id, const char *field)
{
    const char *args[] = { "config", "--unset", NULL, NULL };
    Str_t key = STR_INIT;
    Str_t out = STR_INIT;
    int code = 0;
    eGsErr_t err;

    str_addf(&key, "shared.%s.%s", id, field);
    args[2] = str_cstr(&key);
    err = git_capture(args, PROC_STDERR_NULL, &out, &code);
    if ((err == GS_OK) && (code != 0) && (code != 5)) {
        err = GS_ERR_GIT;
    }
    str_free(&out);
    str_free(&key);
    return err;
}

/* Fine dell'attesa: per ultima, cosi' un'interruzione si puo' ripetere. */
static eGsErr_t end_pending(SharedEntry_t *e)
{
    /* "pending" per prima: se ci si ferma dopo, restano solo chiavi
     * accessorie, ignorate senza "pending". Il contrario lascerebbe
     * un'attesa senza il suo branch (D-077). */
    eGsErr_t err = config_unset(e->id, "pending");

    if ((err == GS_OK) && (e->pending_src.len > 0U)) {
        err = config_unset(e->id, "pendingsource");
    }
    if ((err == GS_OK) && (e->pending_path.len > 0U)) {
        err = config_unset(e->id, "pendingpath");
    }
    if ((err == GS_OK) && (e->pending_branch.len > 0U)) {
        err = config_unset(e->id, "pendingbranch");
    }
    if ((err == GS_OK) && (e->pending_commit.len > 0U)) {
        err = config_unset(e->id, "pendingcommit");
    }
    if (err == GS_OK) {
        e->pending = SHARED_PENDING_NONE;
        e->in_config = 1U;
        str_reset(&e->pending_src);
        str_reset(&e->pending_path);
        str_reset(&e->pending_branch);
        str_reset(&e->pending_commit);
    }
    return err;
}

static eGsErr_t complete_set(const Repo_t *r, SharedEntry_t *e,
                             const SharedEntry_t *committed, const Oid_t *blob,
                             GitBatch_t *b)
{
    Str_t ref = STR_INIT;
    Oid_t tip;
    eGsErr_t err = copy_branches(r, e, committed);

    if (err == GS_OK) {
        chain_ref(&ref, e->id);
        if (git_batch_query(b, str_cstr(&ref), &tip) != GIT_OBJ_COMMIT) {
            err = chain_create_first(e->id, blob, str_cstr(&committed->name));
            if (err != GS_OK) {
                msg_error(MSG_ERR_CHAIN, str_cstr(&committed->name));
            }
        }
    }
    if (err == GS_OK) {
        err = end_pending(e);
    }
    str_free(&ref);
    return err;
}

/*
 * Completa un link (REQ-F-002, D-059): catena dalla ref temporanea del
 * fetch fatto al comando, membership (peer della sorgente + la sorgente),
 * annuncio ai peer.
 */
static eGsErr_t complete_link(const Repo_t *r, SharedEntry_t *e,
                              const SharedEntry_t *committed, GitBatch_t *b)
{
    PeerTally_t tally = { 0U, 0U, 0U };
    Str_t ref = STR_INIT;
    Str_t incoming = STR_INIT;
    Str_t found = STR_INIT;
    Str_t del = STR_INIT;
    const char *p;
    uint8_t ok = 0U;
    Oid_t tip;
    eGsErr_t err = copy_branches(r, e, committed);

    if (err != GS_OK) {
        goto exit;
    }

    /* 1. Catena: la ref temporanea diventa refs/shared/<id>. */
    chain_ref(&ref, e->id);
    peers_incoming_ref(&incoming, e->id, 0U);
    if (git_batch_query(b, str_cstr(&ref), &tip) != GIT_OBJ_COMMIT) {
        if (git_batch_query(b, str_cstr(&incoming), &tip) != GIT_OBJ_COMMIT) {
            /* ref temporanea persa: si rifa' il fetch dalla sorgente */
            str_addz(&found, str_cstr(&e->pending_src));
            peers_fetch(str_cstr(&committed->path), e->id, &found, &ok, &tally);
            str_reset(&found);
        }
        if (git_batch_query(b, str_cstr(&incoming), &tip) != GIT_OBJ_COMMIT) {
            msg_error(MSG_ERR_CHAIN, str_cstr(&committed->name));
            err = GS_ERR_GIT;
            goto exit;
        }
        err = git_update_ref(str_cstr(&ref), &tip, NULL, "git shared: link");
        if (err != GS_OK) {
            goto exit;
        }
    }
    if (git_batch_query(b, str_cstr(&incoming), &tip) == GIT_OBJ_COMMIT) {
        str_addz(&del, str_cstr(&incoming));
        (void)git_delete_refs(&del);
    }

    /* 2. Membership: i peer della sorgente piu' la sorgente, senza se'. */
    if (peers_of(str_cstr(&e->pending_src), e->id, &found) != GS_OK) {
        msg_warning(MSG_WARN_PEER_UNREACHABLE, str_cstr(&committed->path),
                    str_cstr(&e->pending_src));
    }
    str_addz(&found, str_cstr(&e->pending_src));
    for (p = str_znext(&found, NULL); (p != NULL) && (err == GS_OK);
         p = str_znext(&found, p)) {
        if (!str_eq(&r->top, p) && !str_zhas(&e->peers, p)) {
            if (str_zcount(&e->peers) >= GS_PEER_MAX) {
                msg_error(MSG_ERR_TOO_MANY_PEERS, str_cstr(&committed->path),
                          GS_PEER_MAX);
                err = GS_ERR_USAGE;
            } else {
                /* aggiunge senza duplicare, anche se ripetuto */
                const char *add[] = { "config", "--replace-all", "--fixed-value",
                                      NULL, p, p, NULL };
                Str_t key = STR_INIT;

                str_addf(&key, "shared.%s.peer", e->id);
                add[3] = str_cstr(&key);
                err = git_run(add, 0U, NULL);
                str_free(&key);
                str_addz(&e->peers, p);
            }
        }
    }
    if (err != GS_OK) {
        goto exit;
    }

    /* 3. Annuncio: ogni peer aggiunge questa repo ai propri (REQ-F-002).
     * Un peer irraggiungibile non blocca (REQ-F-008, B-07). */
    peers_announce(e->id, &e->peers, str_cstr(&r->top), &tally);

    err = end_pending(e);

exit:
    str_free(&del);
    str_free(&found);
    str_free(&incoming);
    str_free(&ref);
    return err;
}

/* Nome da mostrare per un'attesa: path, altrimenti ID. */
static const char *pending_label(const SharedEntry_t *e)
{
    return (e->pending_path.len > 0U) ? str_cstr(&e->pending_path) : e->id;
}

/* Attesa non committata e sparita dal working tree: annullata. */
static eGsErr_t discard_pending(SharedEntry_t *e, GitBatch_t *b)
{
    Str_t section = STR_INIT;
    Str_t incoming = STR_INIT;
    Str_t del = STR_INIT;
    Oid_t tip;
    eGsErr_t err;

    str_addf(&section, "shared.%s", e->id);
    err = config_run("--remove-section", str_cstr(&section), NULL);
    if (err == GS_OK) {
        if (e->pending == SHARED_PENDING_LINK) {
            peers_incoming_ref(&incoming, e->id, 0U);
            if (git_batch_query(b, str_cstr(&incoming), &tip) == GIT_OBJ_COMMIT) {
                str_addz(&del, str_cstr(&incoming));
                (void)git_delete_refs(&del);
            }
        }
        msg_warning(MSG_WARN_DISCARDED, pending_label(e));
        e->in_config = 0U;
        e->pending = SHARED_PENDING_NONE;
    }
    str_free(&del);
    str_free(&incoming);
    str_free(&section);
    return err;
}

/*
 * Prima di scartare un'attesa (D-079): cerca l'ID nel .gitshared
 * committato di ogni branch locale. Il commit di conferma puo' essere su
 * un branch diverso da quello del comando (git switch -c, poi commit).
 * Quasi tutti i branch hanno lo stesso .gitshared: si legge una volta
 * sola ogni blob distinto. Se lo trova, `declared` contiene quel
 * .gitshared e `rev` il nome della ref.
 */
static eGsErr_t find_on_branches(const char *id, GitBatch_t *b,
                                 Repo_t *declared, Str_t *rev,
                                 const SharedEntry_t **committed)
{
    static const char *const args[] = { "for-each-ref", "--format=%(refname)",
                                        "refs/heads", NULL };
    OidList_t seen = OIDLIST_INIT;
    Str_t refs = STR_INIT;
    Str_t name = STR_INIT;
    char *line;
    char *nl;
    Oid_t blob;
    eGsErr_t err = git_run(args, 0U, &refs);

    *committed = NULL;
    line = (err == GS_OK) ? refs.buf : NULL;
    while ((line != NULL) && (*line != '\0') && (*committed == NULL) &&
           (err == GS_OK)) {
        nl = strchr(line, '\n');
        if (nl != NULL) {
            *nl = '\0';
        }
        str_reset(&name);
        str_addf(&name, "%s:.gitshared", line);
        if (git_batch_blob(b, str_cstr(&name), &blob) &&
            !oidlist_has(&seen, &blob)) {
            oidlist_add(&seen, &blob);
            repo_close(declared);
            err = repo_load_gitshared(declared, str_cstr(&name), 1, 1);
            if (err == GS_OK) {
                *committed = repo_find_id(declared, id);
            }
            if (*committed != NULL) {
                str_reset(rev);
                str_add(rev, line);
            }
        }
        line = (nl != NULL) ? (nl + 1) : NULL;
    }
    str_free(&name);
    str_free(&refs);
    oidlist_free(&seen);
    return err;
}

eGsErr_t repo_declaring_branch(const char *id, GitBatch_t *b, Str_t *branch,
                               int *found)
{
    static const char heads[] = "refs/heads/";
    Repo_t declared = REPO_INIT;
    Str_t rev = STR_INIT;
    const SharedEntry_t *committed = NULL;
    eGsErr_t err = find_on_branches(id, b, &declared, &rev, &committed);

    *found = (err == GS_OK) && (committed != NULL);
    if (*found) {
        str_reset(branch);
        str_add(branch, (strncmp(str_cstr(&rev), heads, sizeof heads - 1U) == 0)
                            ? (str_cstr(&rev) + (sizeof heads - 1U))
                            : str_cstr(&rev));
    }
    repo_close(&declared);
    str_free(&rev);
    return err;
}

/* ---- unlink (REQ-F-003, D-081) ------------------------------------------ */

eGsErr_t repo_rev_declares(GitBatch_t *b, const char *rev, const char *id,
                           int *declared)
{
    Repo_t d = REPO_INIT;
    Str_t name = STR_INIT;
    Oid_t oid;
    eGsErr_t err = GS_OK;

    *declared = 0;
    str_addf(&name, "%s:.gitshared", rev);
    if (git_batch_blob(b, str_cstr(&name), &oid)) {
        err = repo_load_gitshared(&d, str_cstr(&name), 1, 0);
        *declared = (err == GS_OK) && (repo_find_id(&d, id) != NULL);
    }
    repo_close(&d);
    str_free(&name);
    return err;
}

eGsErr_t repo_forget(const Repo_t *r, SharedEntry_t *e, GitBatch_t *b)
{
    PeerTally_t tally = { 0U, 0U, 0U };
    Str_t refs = STR_INIT;
    Str_t name = STR_INIT;
    Oid_t oid;
    size_t k;
    size_t n = str_zcount(&e->peers) + 1U;
    eGsErr_t err;

    /* 1. I peer dimenticano questa repo (B-07: irraggiungibile = avviso). */
    peers_forget(pending_label(e), e->id, &e->peers, str_cstr(&r->top), &tally);

    /* 2. Catena e ref di lavoro. */
    chain_ref(&name, e->id);
    if (git_batch_query(b, str_cstr(&name), &oid) == GIT_OBJ_COMMIT) {
        str_addz(&refs, str_cstr(&name));
    }
    merge_ref(&name, e->id);
    if (git_batch_query(b, str_cstr(&name), &oid) == GIT_OBJ_COMMIT) {
        str_addz(&refs, str_cstr(&name));
    }
    for (k = 0U; k < n; k++) {
        peers_incoming_ref(&name, e->id, (unsigned)k);
        if (git_batch_query(b, str_cstr(&name), &oid) == GIT_OBJ_COMMIT) {
            str_addz(&refs, str_cstr(&name));
        }
    }
    err = git_delete_refs(&refs);

    /* 3. Per ultima la sezione locale, attesa compresa: se ci si ferma
     * prima, il comando successivo ripete tutto. */
    if (err == GS_OK) {
        str_reset(&name);
        str_addf(&name, "shared.%s", e->id);
        err = config_run("--remove-section", str_cstr(&name), NULL);
    }
    if (err == GS_OK) {
        e->in_config = 0U;
        e->pending = SHARED_PENDING_NONE;
        str_reset(&e->peers);
        str_reset(&e->branches);
        str_reset(&e->pending_path);
        str_reset(&e->pending_branch);
        str_reset(&e->pending_commit);
    }
    str_free(&name);
    str_free(&refs);
    return err;
}

eGsErr_t repo_set_self(Repo_t *r, const char *path)
{
    const char *args[] = { "config", "shared.selfPath", path, NULL };
    eGsErr_t err = git_run(args, 0U, NULL);

    if (err == GS_OK) {
        str_reset(&r->self_path);
        str_add(&r->self_path, path);
    }
    return err;
}

void repo_check_self(Repo_t *r)
{
    Str_t dotgit = STR_INIT;
    size_t i;
    int linked = 0;

    for (i = 0U; (i < r->n) && !linked; i++) {
        linked = r->v[i].in_config;
    }
    str_addf(&dotgit, "%s/.git", str_cstr(&r->top));
    if (!linked || !sys_is_dir(str_cstr(&dotgit))) {
        /* nessun file collegato, o worktree secondario */
    } else if (r->self_path.len == 0U) {
        (void)repo_set_self(r, str_cstr(&r->top));
    } else if (!str_eq(&r->top, str_cstr(&r->self_path))) {
        msg_warning(MSG_WARN_MOVED, str_cstr(&r->self_path));
    } else {
        /* al suo posto */
    }
    str_free(&dotgit);
}

/* 1 se a e' antenato di b (o uguale). */
static int is_ancestor(const char *a, const char *b)
{
    const char *args[] = { "merge-base", "--is-ancestor", a, b, NULL };
    Str_t out = STR_INIT;
    int code = 1;

    if (git_capture(args, PROC_STDERR_NULL, &out, &code) != GS_OK) {
        code = 1;
    }
    str_free(&out);
    return code == 0;
}

/*
 * Commit di conferma di un unlink tra le revisioni date (elenco compatto):
 * una revisione che discende dal commit del comando e il cui .gitshared
 * non dichiara piu' l'ID. Un branch nato prima del comando non discende da
 * quel commit; il commit del comando dichiara l'ID. Ogni punta e ogni
 * .gitshared distinti si guardano una volta sola.
 */
static eGsErr_t unlink_confirmed(const SharedEntry_t *e, GitBatch_t *b,
                                 const Str_t *revs, int *found)
{
    OidList_t tips = OIDLIST_INIT;
    OidList_t with = OIDLIST_INIT;    /* .gitshared che dichiarano l'ID */
    OidList_t without = OIDLIST_INIT; /* .gitshared che non lo dichiarano */
    Str_t name = STR_INIT;
    const char *rev;
    Oid_t tip;
    Oid_t blob;
    int declared = 0;
    eGsErr_t err = GS_OK;

    *found = 0;
    for (rev = str_znext(revs, NULL); (rev != NULL) && (*found == 0) && (err == GS_OK);
         rev = str_znext(revs, rev)) {
        if ((git_batch_query(b, rev, &tip) != GIT_OBJ_COMMIT) ||
            oidlist_has(&tips, &tip)) {
            continue;
        }
        oidlist_add(&tips, &tip);
        str_reset(&name);
        str_addf(&name, "%s:.gitshared", rev);
        if (!git_batch_blob(b, str_cstr(&name), &blob)) {
            declared = 0; /* .gitshared tolto del tutto */
        } else if (oidlist_has(&with, &blob)) {
            declared = 1;
        } else if (oidlist_has(&without, &blob)) {
            declared = 0;
        } else {
            err = repo_rev_declares(b, rev, e->id, &declared);
            oidlist_add(declared ? &with : &without, &blob);
        }
        if ((err == GS_OK) && (declared == 0) &&
            is_ancestor(str_cstr(&e->pending_commit), rev)) {
            *found = 1;
        }
    }
    oidlist_free(&without);
    oidlist_free(&with);
    oidlist_free(&tips);
    str_free(&name);
    return err;
}

/* Tutti i branch locali, in un elenco compatto. */
static eGsErr_t local_branches(Str_t *out)
{
    static const char *const args[] = { "for-each-ref", "--format=%(refname)",
                                        "refs/heads", NULL };
    Str_t lines = STR_INIT;
    char *line;
    char *nl;
    eGsErr_t err = git_run(args, 0U, &lines);

    line = (err == GS_OK) ? lines.buf : NULL;
    while ((line != NULL) && (*line != '\0')) {
        nl = strchr(line, '\n');
        if (nl != NULL) {
            *nl = '\0';
        }
        str_addz(out, line);
        line = (nl != NULL) ? (nl + 1) : NULL;
    }
    str_free(&lines);
    return err;
}

/*
 * Un'attesa di unlink (D-081): completa, scarta o lascia com'e'.
 * Prima il branch del comando e HEAD; tutti i branch solo se si sta per
 * scartare o il branch del comando non c'e' piu' (come D-079).
 */
static eGsErr_t settle_unlink(Repo_t *r, SharedEntry_t *e, GitBatch_t *b)
{
    Str_t revs = STR_INIT;
    Str_t name = STR_INIT;
    Oid_t oid;
    int on_branch = 1;
    int exists = 1;
    int found = 0;
    eGsErr_t err = GS_OK;

    if (e->pending_branch.len > 0U) {
        str_addf(&name, "refs/heads/%s", str_cstr(&e->pending_branch));
        on_branch = str_eq(&r->branch, str_cstr(&e->pending_branch));
        exists = (git_batch_query(b, str_cstr(&name), &oid) == GIT_OBJ_COMMIT);
        if (exists) {
            str_addz(&revs, str_cstr(&name));
        }
    }
    str_addz(&revs, "HEAD");
    err = unlink_confirmed(e, b, &revs, &found);
    if ((err == GS_OK) && (found == 0) &&
        ((on_branch && e->in_gitshared) || !exists)) {
        str_reset(&revs);
        err = local_branches(&revs);
        if (err == GS_OK) {
            err = unlink_confirmed(e, b, &revs, &found);
        }
    }

    if ((err == GS_OK) && (found != 0)) {
        err = repo_forget(r, e, b);
    } else if ((err == GS_OK) && on_branch && e->in_gitshared) {
        /* la sezione e' tornata (git restore): unlink annullato */
        msg_warning(MSG_WARN_DISCARDED, pending_label(e));
        err = end_pending(e);
    } else if ((err == GS_OK) && !exists) {
        msg_warning(MSG_WARN_PENDING_NO_BRANCH, pending_label(e),
                    str_cstr(&e->pending_branch), e->id);
    } else {
        /* ancora in attesa di commit */
    }
    str_free(&name);
    str_free(&revs);
    return err;
}

/* Un'attesa (set o link): completa, scarta o lascia com'e' (D-077). */
static eGsErr_t settle_pending(Repo_t *r, SharedEntry_t *e, GitBatch_t *b)
{
    Repo_t declared = REPO_INIT; /* .gitshared committato */
    Str_t revs = STR_INIT;       /* dove cercare, elenco compatto */
    Str_t other = STR_INIT;      /* branch trovato dalla ricerca estesa */
    Str_t name = STR_INIT;
    const SharedEntry_t *committed = NULL;
    const char *rev = NULL;
    Oid_t oid;
    int on_branch = 1;
    int exists = 1;
    eGsErr_t err = GS_OK;

    /* 1. Dove cercare il commit di conferma: prima il branch del comando,
     * poi HEAD (l'utente puo' aver portato .gitshared in staging su un
     * altro branch e committato li'). Senza branch registrato, solo HEAD. */
    if (e->pending_branch.len > 0U) {
        str_addf(&name, "refs/heads/%s", str_cstr(&e->pending_branch));
        on_branch = str_eq(&r->branch, str_cstr(&e->pending_branch));
        exists = (git_batch_query(b, str_cstr(&name), &oid) == GIT_OBJ_COMMIT);
        if (exists) {
            str_addz(&revs, str_cstr(&name));
        }
    }
    if ((e->pending_branch.len == 0U) || !on_branch || !exists) {
        str_addz(&revs, "HEAD"); /* sul branch del comando HEAD e' gia' li' */
    }

    /* 2. Sezione committata. Un branch ancora senza commit (set prima del
     * primo commit) non ha .gitshared: si aspetta. */
    rev = str_znext(&revs, NULL);
    while ((rev != NULL) && (committed == NULL) && (err == GS_OK)) {
        repo_close(&declared);
        str_reset(&name);
        str_addf(&name, "%s:.gitshared", rev);
        err = repo_load_gitshared(&declared, str_cstr(&name), 1, 1);
        committed = (err == GS_OK) ? repo_find_id(&declared, e->id) : NULL;
        if (committed == NULL) {
            rev = str_znext(&revs, rev);
        }
    }

    /* 2b. Si sta per scartare (sul branch del comando, sezione sparita
     * dal working tree): prima si guarda se un altro branch la dichiara
     * (D-079, reportIdea B-39). */
    if ((committed == NULL) && (err == GS_OK) && on_branch && !e->in_gitshared) {
        err = find_on_branches(e->id, b, &declared, &other, &committed);
        rev = str_cstr(&other);
    }

    /* 3. Completamento, da qualunque branch. */
    if (committed != NULL) {
        str_reset(&name);
        str_addf(&name, "%s:%s", rev, str_cstr(&committed->path));
        if (git_batch_blob(b, str_cstr(&name), &oid) == 0) {
            /* .gitshared committato ma il file no: si aspetta */
            msg_warning(MSG_WARN_NOT_IN_HEAD, str_cstr(&committed->path));
        } else if (e->pending == SHARED_PENDING_SET) {
            err = complete_set(r, e, committed, &oid, b);
        } else {
            err = complete_link(r, e, committed, b);
        }
    } else if ((err == GS_OK) && on_branch && !e->in_gitshared) {
        /* 4. Sul branch del comando la sezione non c'e' piu' ne' nel
         * commit ne' nel working tree: annullata (git restore). */
        err = discard_pending(e, b);
    } else if ((err == GS_OK) && !exists && !on_branch) {
        /* branch cancellato o rinominato: decide l'utente */
        msg_warning(MSG_WARN_PENDING_NO_BRANCH, pending_label(e),
                    str_cstr(&e->pending_branch), e->id);
    } else {
        /* ancora in attesa di commit, o su un altro branch: resta */
    }

    repo_close(&declared);
    str_free(&name);
    str_free(&other);
    str_free(&revs);
    return err;
}

eGsErr_t repo_complete_pending(Repo_t *r, GitBatch_t *b)
{
    size_t i;
    eGsErr_t err = GS_OK;

    for (i = 0U; (i < r->n) && (err == GS_OK); i++) {
        if ((r->v[i].pending == SHARED_PENDING_SET) ||
            (r->v[i].pending == SHARED_PENDING_LINK)) {
            err = settle_pending(r, &r->v[i], b);
        } else if (r->v[i].pending == SHARED_PENDING_UNLINK) {
            err = settle_unlink(r, &r->v[i], b);
        } else {
            /* nessuna attesa */
        }
    }
    return err;
}
