/*
 * shared.c - la repo corrente e i suoi file shared.
 */
#include "shared.h"

#include <stdlib.h>
#include <string.h>

#include "chain.h"
#include "mem.h"
#include "msg.h"
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

int shared_is_active(const SharedEntry_t *e, const char *branch)
{
    const char *b = str_cstr(&e->branches);
    const char *end = b + e->branches.len;
    int active = 0;

    while ((branch[0] != '\0') && (b < end) && (active == 0)) {
        active = (strcmp(b, branch) == 0);
        b += strlen(b) + 1U;
    }
    return active;
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

static void add_branch(SharedEntry_t *e, const char *branch)
{
    str_add(&e->branches, branch);
    str_addch(&e->branches, '\0');
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
            add_branch(e, k.value);
        } else {
            /* altri campi (url: fase 2) */
        }
    }
}

/* Record di .git/config (scope locale): sezione per ID. */
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
                    e->pending = i;
                }
            }
        } else if (field_is(&k, "branch")) {
            add_branch(e, k.value);
        } else {
            /* peer: M2 */
        }
    }
}

/*
 * Carica un .gitshared: dal working tree (file) o da un commit (blob, es.
 * "HEAD:.gitshared"). Un blob mancante o illeggibile da' zero voci.
 */
static eGsErr_t load_gitshared(Repo_t *r, const char *src, int is_blob,
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
     * valido (verificato con git 2.43). */
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

eGsErr_t repo_open(Repo_t *r)
{
    static const char *const a_top[] = { "rev-parse", "--show-toplevel",
                                         "--show-prefix", NULL };
    static const char *const a_head[] = { "symbolic-ref", "-q", "HEAD", NULL };
    static const char *const a_cfg[] = { "config", "-z", "--show-scope",
                                         "--get-regexp", "^shared\\.", NULL };
    static const char heads[] = "refs/heads/";
    Proc_t p_top;
    Proc_t p_head;
    Proc_t p_cfg;
    Str_t out = STR_INIT;
    Str_t file = STR_INIT;
    const char *nl;
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

    /* Radice e prefisso: una riga ciascuno (il prefisso puo' essere vuoto). */
    run &= ~RUN_TOP;
    err = proc_collect(&p_top, &out, &code);
    if (err != GS_OK) {
        goto exit;
    }
    nl = strchr(str_cstr(&out), '\n');
    if ((code != 0) || (nl == NULL) || (nl == out.buf)) {
        err = GS_ERR_NOT_REPO; /* anche dentro .git: nessun working tree */
        goto exit;
    }
    str_addn(&r->top, out.buf, (size_t)(nl - out.buf));
    str_add(&r->prefix, nl + 1);
    str_trim_eol(&r->prefix);

    /* .gitshared mentre gli altri due processi finiscono. */
    str_addf(&file, "%s/.gitshared", str_cstr(&r->top));
    err = load_gitshared(r, str_cstr(&file), 0, 0);
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

    /* Record "scope\0chiave\nvalore\0": contano solo quelli locali. */
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
            if (strcmp(scope, "local") == 0) {
                parse_local_rec(r, rec);
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

/* Esegue "git config <a1> <a2> [<a3>]" (a3 puo' essere NULL). */
static eGsErr_t config_set(const char *a1, const char *a2, const char *a3)
{
    const char *args[] = { "config", a1, a2, a3, NULL };

    return git_run(args, 0U, NULL);
}

/*
 * Completa un `set` committato. Ordine pensato per poter ripetere tutto
 * dopo un'interruzione: branch attivi, catena (solo se manca), e per
 * ultima la rimozione dell'attesa.
 */
static eGsErr_t complete_set(Repo_t *r, SharedEntry_t *e,
                             const SharedEntry_t *committed, GitBatch_t *b)
{
    const char *unset_args[] = { "config", "--unset-all", NULL, NULL };
    const Str_t *list = &committed->branches;
    Str_t name = STR_INIT;
    Str_t key = STR_INIT;
    Str_t current = STR_INIT;
    Str_t ignored = STR_INIT;
    Oid_t blob;
    Oid_t tip;
    const char *br;
    const char *end;
    int code = 0;
    eGsErr_t err = GS_OK;

    str_addf(&name, "HEAD:%s", str_cstr(&committed->path));
    if (git_batch_blob(b, str_cstr(&name), &blob) == 0) {
        msg_warning(MSG_WARN_NOT_IN_HEAD, str_cstr(&committed->path));
        goto exit; /* resta in attesa */
    }

    /* 1. Branch attivi: copia del default (D-057); se manca, il corrente. */
    if (list->len == 0U) {
        if (r->branch.len > 0U) {
            str_add(&current, str_cstr(&r->branch));
            str_addch(&current, '\0');
        } else {
            msg_warning(MSG_WARN_NO_BRANCH, str_cstr(&committed->name), e->id);
        }
        list = &current;
    }
    str_addf(&key, "shared.%s.branch", e->id);
    unset_args[2] = str_cstr(&key);
    err = git_capture(unset_args, 0U, &ignored, &code); /* 5 = non c'era */
    if (err != GS_OK) {
        goto exit;
    }
    str_reset(&e->branches);
    br = str_cstr(list);
    end = br + list->len;
    while ((br < end) && (err == GS_OK)) {
        err = config_set("--add", str_cstr(&key), br);
        add_branch(e, br);
        br += strlen(br) + 1U;
    }
    if (err != GS_OK) {
        goto exit;
    }

    /* 2. Catena, se non esiste gia'. */
    str_reset(&name);
    str_addf(&name, "refs/shared/%s", e->id);
    if (git_batch_query(b, str_cstr(&name), &tip) != GIT_OBJ_COMMIT) {
        err = chain_create_first(e->id, &blob, str_cstr(&committed->name));
        if (err != GS_OK) {
            msg_error(MSG_ERR_CHAIN, str_cstr(&committed->name));
            goto exit;
        }
    }

    /* 3. Fine dell'attesa. */
    str_reset(&key);
    str_addf(&key, "shared.%s.pending", e->id);
    err = config_set("--unset", str_cstr(&key), NULL);
    if (err == GS_OK) {
        e->pending = SHARED_PENDING_NONE;
        e->in_config = 1U;
    }

exit:
    str_free(&ignored);
    str_free(&current);
    str_free(&key);
    str_free(&name);
    return err;
}

eGsErr_t repo_complete_pending(Repo_t *r, GitBatch_t *b)
{
    Repo_t head = REPO_INIT;
    Str_t section = STR_INIT;
    const SharedEntry_t *committed;
    SharedEntry_t *e;
    size_t i;
    int loaded = 0;
    eGsErr_t err = GS_OK;

    for (i = 0U; (i < r->n) && (err == GS_OK); i++) {
        e = &r->v[i];
        if (e->pending == SHARED_PENDING_SET) {
            if (loaded == 0) {
                /* Letto una sola volta, e solo se c'e' qualcosa in attesa. */
                err = load_gitshared(&head, "HEAD:.gitshared", 1, 1);
                loaded = 1;
            }
            committed = (err == GS_OK)
                            ? find_sub(&head, e->id, strlen(e->id), 1)
                            : NULL;
            if (committed != NULL) {
                err = complete_set(r, e, committed, b);
            } else if ((err == GS_OK) && !e->in_gitshared) {
                /* Ne' committata ne' nel working tree: annullata. */
                str_reset(&section);
                str_addf(&section, "shared.%s", e->id);
                err = config_set("--remove-section", str_cstr(&section), NULL);
                if (err == GS_OK) {
                    msg_warning(MSG_WARN_DISCARDED, e->id);
                    e->in_config = 0U;
                    e->pending = SHARED_PENDING_NONE;
                }
            } else {
                /* ancora in attesa di commit */
            }
        }
    }
    str_free(&section);
    repo_close(&head);
    return err;
}
