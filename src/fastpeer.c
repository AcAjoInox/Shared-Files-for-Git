/*
 * fastpeer.c - update --peers senza processi per i casi semplici (D-116).
 * Il modello e i limiti sono in fastpeer.h.
 */
#include "fastpeer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mem.h"
#include "msg.h"
#include "sha.h"
#include "sys.h"

/* ---- peer e catene ---------------------------------------------------- */

static void *zalloc(size_t n, size_t size)
{
    void *p = xmalloc(((n > 0U) ? n : 1U) * size);

    memset(p, 0, ((n > 0U) ? n : 1U) * size);
    return p;
}

void fp_peer_init(FpPeer_t *pp, const char *root)
{
    str_init(&pp->root);
    str_init(&pp->git_dir);
    str_init(&pp->ids);
    str_init(&pp->out);
    str_init(&pp->src);
    str_add(&pp->root, root);
    str_addf(&pp->git_dir, "%s/.git", root);
    pp->f = NULL;
    pp->n = 0U;
    pp->cap = 0U;
    pp->kind = FPP_FAST;
    pp->rc = 0;
    pp->started = 0;
    pp->done = 0;
}

void fp_peer_free(FpPeer_t *pp)
{
    size_t i;

    for (i = 0U; i < pp->n; i++) {
        str_free(&pp->f[i].path);
    }
    free(pp->f);
    pp->f = NULL;
    pp->n = 0U;
    str_free(&pp->root);
    str_free(&pp->git_dir);
    str_free(&pp->ids);
    str_free(&pp->out);
    str_free(&pp->src);
}

void fp_peer_add(FpPeer_t *pp, size_t chain, const char *id)
{
    FpFile_t *f;

    if (pp->n == pp->cap) {
        pp->cap = (pp->cap == 0U) ? 4U : (2U * pp->cap);
        pp->f = xrealloc(pp->f, pp->cap * sizeof *pp->f);
    }
    f = &pp->f[pp->n++];
    f->chain = chain;
    str_init(&f->path);
    f->tp.len = 0U;
    f->head.len = 0U;
    f->kind = FP_FALLBACK;
    if (pp->ids.len > 0U) {
        str_addch(&pp->ids, ',');
    }
    str_add(&pp->ids, id);
}

void fp_chain_free(FpChain_t *c)
{
    free(c->v);
    c->v = NULL;
    c->n = 0U;
    c->cap = 0U;
}

/* Il peer va al figlio di D-115: nessun file nel percorso veloce. */
static void to_fallback(FpPeer_t *pp)
{
    size_t i;

    pp->kind = FPP_FALLBACK;
    for (i = 0U; i < pp->n; i++) {
        pp->f[i].kind = FP_FALLBACK;
    }
}

static void out_line(FpPeer_t *pp, eMsgId_t id, const char *arg)
{
    str_addf(&pp->out, msg(id), arg);
    str_addch(&pp->out, '\n');
}

/* ---- file -------------------------------------------------------------- */

/* Tutto il file in out. 0 se non si apre (anche se non c'e'). */
static int read_file(const char *path, Str_t *out)
{
    char buf[4096];
    FILE *f = sys_fopen(path, "rb");
    size_t got;
    int ok = 0;

    str_reset(out);
    if (f != NULL) {
        ok = 1;
        while ((got = fread(buf, 1U, sizeof buf, f)) > 0U) {
            str_addn(out, buf, got);
        }
        if (ferror(f)) {
            ok = 0;
        }
        (void)fclose(f);
    }
    return ok;
}

static int file_exists(const char *path)
{
    FILE *f = sys_fopen(path, "rb");

    if (f != NULL) {
        (void)fclose(f);
    }
    return (f != NULL) || sys_is_dir(path);
}

/* ---- lettore degli oggetti --------------------------------------------- */

eGsErr_t fp_reader_open(FpReader_t *rd, FpPeer_t *peers, size_t n)
{
    static const char *const args[] = { "cat-file", "--batch", NULL };
    static const char var[] = "GIT_ALTERNATE_OBJECT_DIRECTORIES";
#ifdef _WIN32
    const char sep = ';';
#else
    const char sep = ':';
#endif
    Str_t alt = STR_INIT;
    Str_t dir = STR_INIT;
    size_t i;
    eGsErr_t err;

    str_init(&rd->line);
    rd->open = 0;
    for (i = 0U; i < n; i++) {
        if (peers[i].kind != FPP_FAST) {
            continue;
        }
        str_reset(&dir);
        str_addf(&dir, "%s/objects", str_cstr(&peers[i].git_dir));
        /* un path con il separatore andrebbe tra virgolette: ripiego */
        if (!sys_is_dir(str_cstr(&dir)) || (strchr(str_cstr(&dir), sep) != NULL) ||
            (dir.buf[0] == '"')) {
            to_fallback(&peers[i]);
            continue;
        }
        if (alt.len > 0U) {
            str_addch(&alt, sep);
        }
        str_add(&alt, str_cstr(&dir));
    }
    if (sys_setenv(var, str_cstr(&alt)) != 0) {
        err = GS_ERR_SYS;
    } else {
        err = git_start(&rd->p, args, PROC_STDIN_PIPE);
        sys_unsetenv(var); /* gli altri processi non la vedono */
    }
    rd->open = (err == GS_OK);
    str_free(&dir);
    str_free(&alt);
    return err;
}

void fp_reader_close(FpReader_t *rd)
{
    int code;

    if (rd->open != 0) {
        (void)proc_finish(&rd->p, &code);
        rd->open = 0;
    }
    str_free(&rd->line);
}

/* Contenuto dell'oggetto oid. GIT_OBJ_ERROR se il dialogo si interrompe
 * (il lettore si chiude: le domande dopo falliscono, e tutto ripiega). */
static eGitObj_t rd_get(FpReader_t *rd, const Oid_t *oid, Str_t *content)
{
    char hex[OID_HEX_SIZE];
    char buf[8192];
    const char *sp;
    const char *type;
    unsigned long size;
    unsigned long left;
    size_t want;
    size_t got;
    eGitObj_t kind = GIT_OBJ_ERROR;
    int code;

    str_reset(content);
    if ((rd->open == 0) || (oid->len == 0U)) {
        goto exit;
    }
    oid_to_hex(oid, hex);
    if ((fprintf(rd->p.in, "%s\n", hex) < 0) || (fflush(rd->p.in) != 0) ||
        !proc_read(&rd->p, '\n', &rd->line)) {
        goto fail;
    }
    /* "<oid> <tipo> <dimensione>" oppure "<oid> missing" */
    sp = strchr(str_cstr(&rd->line), ' ');
    if (sp == NULL) {
        goto fail;
    }
    type = sp + 1;
    if (strcmp(type, "missing") == 0) {
        kind = GIT_OBJ_MISSING;
        goto exit;
    }
    sp = strchr(type, ' ');
    if (sp == NULL) {
        goto fail;
    }
    size = strtoul(sp + 1, NULL, 10);
    if (strncmp(type, "blob ", 5U) == 0) {
        kind = GIT_OBJ_BLOB;
    } else if (strncmp(type, "tree ", 5U) == 0) {
        kind = GIT_OBJ_TREE;
    } else if (strncmp(type, "commit ", 7U) == 0) {
        kind = GIT_OBJ_COMMIT;
    } else {
        kind = GIT_OBJ_OTHER;
    }
    for (left = size; left > 0UL; left -= (unsigned long)got) {
        want = (left < sizeof buf) ? (size_t)left : sizeof buf;
        got = fread(buf, 1U, want, rd->p.out);
        if (got == 0U) {
            goto fail;
        }
        str_addn(content, buf, got);
    }
    if (getc(rd->p.out) != '\n') {
        goto fail;
    }
    goto exit;

fail:
    kind = GIT_OBJ_ERROR;
    (void)proc_finish(&rd->p, &code);
    rd->open = 0;
exit:
    return kind;
}

/* ---- commit e tree ----------------------------------------------------- */

/* tree e genitori di un commit (al piu' 2). 0 se non torna. */
static int parse_commit(const Str_t *c, size_t hexlen, Oid_t *tree,
                        Oid_t parents[2], unsigned *np)
{
    const char *p = str_cstr(c);
    const char *end = p + c->len;
    const char *nl;
    int ok = 1;

    tree->len = 0U;
    *np = 0U;
    while ((p < end) && (*p != '\n') && ok) {
        nl = memchr(p, '\n', (size_t)(end - p));
        if (nl == NULL) {
            ok = 0;
        } else if (strncmp(p, "tree ", 5U) == 0) {
            ok = ((size_t)(nl - (p + 5)) == hexlen) && oid_from_hex(tree, p + 5, hexlen);
        } else if (strncmp(p, "parent ", 7U) == 0) {
            ok = (*np < 2U) && ((size_t)(nl - (p + 7)) == hexlen) &&
                 oid_from_hex(&parents[*np], p + 7, hexlen);
            (*np)++;
        } else {
            /* author, committer, ...: non servono */
        }
        p = (nl != NULL) ? (nl + 1) : end;
    }
    return ok && (tree->len > 0U);
}

/*
 * Voce successiva di un tree, da *pos: modo, nome (termina con '\0' nel
 * contenuto) e oid. 0 a fine tree, -1 se il tree non torna.
 */
static int tree_next(const Str_t *t, size_t *pos, size_t rawlen,
                     const char **mode, const char **name, Oid_t *oid)
{
    const char *p = str_cstr(t) + *pos;
    const char *end = str_cstr(t) + t->len;
    const char *sp;
    const char *nul;
    int rc = -1;

    if (p >= end) {
        rc = 0;
    } else {
        sp = memchr(p, ' ', (size_t)(end - p));
        nul = (sp != NULL) ? memchr(sp, '\0', (size_t)(end - sp)) : NULL;
        if ((nul != NULL) && ((size_t)(end - (nul + 1)) >= rawlen)) {
            *mode = p;
            *name = sp + 1;
            oid->len = (uint8_t)rawlen;
            memcpy(oid->raw, nul + 1, rawlen);
            *pos = (size_t)((nul + 1 + rawlen) - str_cstr(t));
            rc = 1;
        }
    }
    return rc;
}

static int mode_is(const char *mode, const char *want)
{
    const size_t n = strlen(want);

    return (strncmp(mode, want, n) == 0) && (mode[n] == ' ');
}

/* Blob del file al path dato, nel tree. 0 se non c'e' o non e' un file. */
static int tree_lookup(FpReader_t *rd, const Oid_t *tree, const char *path,
                       Oid_t *blob)
{
    Str_t t = STR_INIT;
    Str_t part = STR_INIT;
    const char *p = path;
    const char *slash;
    const char *mode;
    const char *name;
    Oid_t cur = *tree;
    Oid_t oid;
    size_t pos;
    int last;
    int found = 1;
    int r;

    while (found && (*p != '\0')) {
        slash = strchr(p, '/');
        last = (slash == NULL);
        str_reset(&part);
        str_addn(&part, p, last ? strlen(p) : (size_t)(slash - p));
        found = 0;
        if (rd_get(rd, &cur, &t) == GIT_OBJ_TREE) {
            pos = 0U;
            while ((r = tree_next(&t, &pos, cur.len, &mode, &name, &oid)) == 1) {
                if (strcmp(name, str_cstr(&part)) == 0) {
                    if (last && (mode_is(mode, "100644") || mode_is(mode, "100755"))) {
                        *blob = oid;
                        found = 1;
                    } else if (!last && mode_is(mode, "40000")) {
                        cur = oid;
                        found = 1;
                    } else {
                        /* link simbolico, submodule, cartella al posto del file */
                    }
                    break;
                }
            }
        }
        p = last ? (p + strlen(p)) : (slash + 1);
    }
    str_free(&part);
    str_free(&t);
    return found;
}

/* ---- catena ------------------------------------------------------------ */

/* Voce dell'autorita' uguale, anche se manca da entrambe le parti
 * (oid_eq da' falso su due oid vuoti; come in auth.c). */
static int entry_eq(const Oid_t *a, const Oid_t *b)
{
    return ((a->len == 0U) && (b->len == 0U)) || oid_eq(a, b);
}

static size_t chain_find(const FpChain_t *c, const Oid_t *oid)
{
    size_t i = 0U;

    while ((i < c->n) && !oid_eq(&c->v[i].commit, oid)) {
        i++;
    }
    return i;
}

static size_t chain_add(FpChain_t *c, const Oid_t *oid)
{
    FpNode_t *v;

    if (c->n == c->cap) {
        c->cap = (c->cap == 0U) ? 16U : (2U * c->cap);
        c->v = xrealloc(c->v, c->cap * sizeof *c->v);
    }
    v = &c->v[c->n];
    memset(v, 0, sizeof *v);
    v->commit = *oid;
    return c->n++;
}

/* Tree di una versione: solo voci blob 100644 note, "content" obbligatoria. */
static int read_version_tree(FpReader_t *rd, FpNode_t *v, Str_t *t)
{
    const char *mode;
    const char *name;
    Oid_t oid;
    Oid_t *slot;
    size_t pos = 0U;
    int r;
    int ok = (rd_get(rd, &v->tree, t) == GIT_OBJ_TREE);

    while (ok && ((r = tree_next(t, &pos, v->tree.len, &mode, &name, &oid)) != 0)) {
        slot = NULL;
        if (r < 0) {
            ok = 0;
        } else if (strcmp(name, "content") == 0) {
            slot = &v->content;
        } else if (strcmp(name, "creator") == 0) {
            slot = &v->creator;
        } else if (strcmp(name, "sovereigns") == 0) {
            slot = &v->sovereigns;
        } else if (strcmp(name, "heirs") == 0) {
            slot = &v->heirs;
        } else {
            ok = 0; /* voce sconosciuta: la catena la legge il figlio */
        }
        if (ok && (slot != NULL)) {
            ok = mode_is(mode, "100644");
            *slot = oid;
        }
    }
    return ok && (v->content.len > 0U);
}

void fp_chain_load(FpReader_t *rd, FpChain_t *c)
{
    Str_t obj = STR_INIT;
    Oid_t parents[2];
    unsigned np;
    unsigned k;
    size_t i;
    size_t j;
    int ok = (c->t.len > 0U);

    c->ok = 0;
    c->n = 0U;
    if (ok) {
        (void)chain_add(c, &c->t);
    }
    /* in ampiezza da T: v[0] = T, i genitori dopo */
    for (i = 0U; ok && (i < c->n); i++) {
        ok = (rd_get(rd, &c->v[i].commit, &obj) == GIT_OBJ_COMMIT) &&
             parse_commit(&obj, 2U * c->t.len, &c->v[i].tree, parents, &np) &&
             read_version_tree(rd, &c->v[i], &obj);
        for (k = 0U; ok && (k < np); k++) {
            j = chain_find(c, &parents[k]);
            if (j == c->n) {
                ok = (c->n < FP_CHAIN_MAX);
                if (ok) {
                    j = chain_add(c, &parents[k]);
                }
            }
            c->v[i].parent[k] = j;
        }
        c->v[i].np = (unsigned char)np;
    }
    c->ok = ok;
    str_free(&obj);
}

/* mark = 1 per le versioni raggiungibili da v[from] (compresa). */
static void chain_mark(FpChain_t *c, size_t from)
{
    size_t *stack = xmalloc(((c->n > 0U) ? c->n : 1U) * sizeof *stack);
    size_t top = 0U;
    size_t i;
    unsigned k;

    for (i = 0U; i < c->n; i++) {
        c->v[i].mark = 0U;
    }
    stack[top++] = from;
    c->v[from].mark = 1U;
    while (top > 0U) {
        i = stack[--top];
        for (k = 0U; k < c->v[i].np; k++) {
            if (c->v[c->v[i].parent[k]].mark == 0U) {
                c->v[c->v[i].parent[k]].mark = 1U;
                stack[top++] = c->v[i].parent[k];
            }
        }
    }
    free(stack);
}

/* ---- config: parser ridotto ------------------------------------------- */

typedef void (*cfg_fn_t)(void *ctx, const char *sect, const char *sub,
                         const char *key, const char *val);

/*
 * Sintassi di git config: sezioni, sottosezioni tra virgolette (o la forma
 * vecchia "sezione.sotto"), chiavi, valori con virgolette, escape,
 * continuazione e commenti. val = NULL per una chiave senza "=". 0 se il
 * testo non torna: chi chiama ripiega.
 */
static int cfg_parse(const Str_t *text, cfg_fn_t fn, void *ctx)
{
    const char *p = str_cstr(text);
    const char *end = p + text->len;
    Str_t sect = STR_INIT;
    Str_t sub = STR_INIT;
    Str_t key = STR_INIT;
    Str_t val = STR_INIT;
    size_t keep;
    int has_sub = 0;
    int quote;
    int ok = 1;
    char c;

    while (ok && (p < end)) {
        c = *p;
        if ((c == ' ') || (c == '\t') || (c == '\r') || (c == '\n')) {
            p++;
        } else if ((c == '#') || (c == ';')) {
            while ((p < end) && (*p != '\n')) {
                p++;
            }
        } else if (c == '[') {
            p++;
            str_reset(&sect);
            str_reset(&sub);
            has_sub = 0;
            while ((p < end) && (isalnum((unsigned char)*p) || (*p == '-') || (*p == '.'))) {
                str_addch(&sect, (char)tolower((unsigned char)*p));
                p++;
            }
            while ((p < end) && ((*p == ' ') || (*p == '\t'))) {
                p++;
            }
            if ((p < end) && (*p == '"')) {
                has_sub = 1;
                for (p++; (p < end) && (*p != '"') && (*p != '\n') && ok; p++) {
                    if (*p == '\\') {
                        p++;
                        ok = (p < end) && (*p != '\n');
                    }
                    if (ok) {
                        str_addch(&sub, *p);
                    }
                }
                ok = ok && (p < end) && (*p == '"');
                p++;
            } else if (strchr(str_cstr(&sect), '.') != NULL) {
                /* forma vecchia [sezione.sotto]: la sotto in minuscolo */
                has_sub = 1;
                str_add(&sub, strchr(str_cstr(&sect), '.') + 1);
                str_setlen(&sect, (size_t)(strchr(str_cstr(&sect), '.') - str_cstr(&sect)));
            } else {
                /* nessuna sottosezione */
            }
            ok = ok && (p < end) && (*p == ']') && (sect.len > 0U);
            p++;
        } else if (isalpha((unsigned char)c) && (sect.len > 0U)) {
            str_reset(&key);
            while ((p < end) && (isalnum((unsigned char)*p) || (*p == '-'))) {
                str_addch(&key, (char)tolower((unsigned char)*p));
                p++;
            }
            while ((p < end) && ((*p == ' ') || (*p == '\t'))) {
                p++;
            }
            if ((p >= end) || (*p == '\n') || (*p == '\r') || (*p == '#') || (*p == ';')) {
                fn(ctx, str_cstr(&sect), has_sub ? str_cstr(&sub) : NULL,
                   str_cstr(&key), NULL);
            } else if (*p != '=') {
                ok = 0;
            } else {
                p++;
                while ((p < end) && ((*p == ' ') || (*p == '\t'))) {
                    p++;
                }
                str_reset(&val);
                quote = 0;
                keep = 0U; /* lunghezza senza gli spazi finali fuori virgolette */
                while (ok && (p < end) && (*p != '\n')) {
                    c = *p++;
                    if (c == '\\') {
                        c = (p < end) ? *p++ : '\0';
                        if (c == '\r') {
                            c = (p < end) ? *p++ : '\0';
                        }
                        if (c == '\n') {
                            continue; /* continuazione */
                        }
                        c = (c == 'n') ? '\n' : (c == 't') ? '\t' : (c == 'b') ? '\b' : c;
                        ok = (c == '\n') || (c == '\t') || (c == '\b') || (c == '"') ||
                             (c == '\\');
                        str_addch(&val, c);
                        keep = val.len;
                    } else if (c == '"') {
                        quote = !quote;
                        keep = val.len;
                    } else if (((c == '#') || (c == ';')) && !quote) {
                        while ((p < end) && (*p != '\n')) {
                            p++;
                        }
                    } else if (c == '\r') {
                        /* CRLF: fine riga */
                    } else {
                        str_addch(&val, c);
                        if (quote || ((c != ' ') && (c != '\t'))) {
                            keep = val.len;
                        }
                    }
                }
                ok = ok && !quote;
                if (ok) {
                    str_setlen(&val, keep);
                    fn(ctx, str_cstr(&sect), has_sub ? str_cstr(&sub) : NULL,
                       str_cstr(&key), str_cstr(&val));
                }
            }
        } else {
            ok = 0;
        }
    }
    str_free(&val);
    str_free(&key);
    str_free(&sub);
    str_free(&sect);
    return ok;
}

static int cfg_is_false(const char *v)
{
    static const char *const no[] = { "false", "no", "off", "0" };
    size_t i;
    size_t j;
    int is_false = 0;

    for (i = 0U; (v != NULL) && (i < (sizeof no / sizeof no[0])) && !is_false; i++) {
        j = 0U;
        while ((no[i][j] != '\0') && (tolower((unsigned char)v[j]) == no[i][j])) {
            j++;
        }
        is_false = (no[i][j] == '\0') && (v[j] == '\0');
    }
    return is_false;
}

/* .git/config del peer: solo cio' che serve, e i motivi di ripiego. */
typedef struct {
    FpPeer_t *pp;
    const FpChain_t *chains;
    unsigned char *in_config; /* per file: sezione [shared "<id>"] */
    unsigned char *pending;   /* per file: attesa (D-113) */
    Str_t *branches;          /* per file: branch attivi, elenco compatto */
    Str_t objfmt;
    int bad;
    int off;
} PeerCfg_t;

static void peer_cfg_fn(void *ctx, const char *sect, const char *sub,
                        const char *key, const char *val)
{
    PeerCfg_t *c = ctx;
    size_t i;

    if ((strcmp(sect, "include") == 0) || (strcmp(sect, "includeif") == 0)) {
        c->bad = 1; /* la config continua altrove */
    } else if ((strcmp(sect, "extensions") == 0) && (sub == NULL)) {
        if (strcmp(key, "objectformat") == 0) {
            str_reset(&c->objfmt);
            str_add(&c->objfmt, (val != NULL) ? val : "");
        } else if (((strcmp(key, "refstorage") == 0) &&
                    ((val == NULL) || (strcmp(val, "files") != 0))) ||
                   ((strcmp(key, "worktreeconfig") == 0) && !cfg_is_false(val))) {
            c->bad = 1;
        } else {
            /* altre estensioni: non toccano ref, oggetti e config */
        }
    } else if ((strcmp(sect, "core") == 0) && (sub == NULL)) {
        if (((strcmp(key, "logallrefupdates") == 0) && (val != NULL) &&
             (strcmp(val, "always") == 0)) ||
            ((strcmp(key, "bare") == 0) && !cfg_is_false(val))) {
            c->bad = 1; /* reflog anche di refs/shared, o repo senza file */
        }
    } else if ((strcmp(sect, "shared") == 0) && (sub == NULL)) {
        if (strcmp(key, "peerupdate") == 0) {
            c->off = cfg_is_false(val);
        }
    } else if ((strcmp(sect, "shared") == 0) && (sub != NULL)) {
        for (i = 0U; i < c->pp->n; i++) {
            if (strcmp(sub, c->chains[c->pp->f[i].chain].id) == 0) {
                c->in_config[i] = 1U;
                if (strcmp(key, "pending") == 0) {
                    c->pending[i] = 1U;
                } else if ((strcmp(key, "branch") == 0) && (val != NULL)) {
                    str_addz(&c->branches[i], val);
                } else {
                    /* peer, authority*, ...: non servono */
                }
            }
        }
    } else {
        /* altre sezioni */
    }
}

/* .gitshared del peer: path di ogni ID chiesto. */
typedef struct {
    FpPeer_t *pp;
    const FpChain_t *chains;
    Str_t names;   /* sezioni, elenco compatto */
    Str_t *ids;    /* per sezione */
    Str_t *paths;  /* per sezione */
    size_t n;
    size_t cap;
} PeerShared_t;

static void peer_shared_fn(void *ctx, const char *sect, const char *sub,
                           const char *key, const char *val)
{
    PeerShared_t *s = ctx;
    const char *p;
    size_t i = 0U;

    if ((strcmp(sect, "shared") != 0) || (sub == NULL) || (val == NULL)) {
        return;
    }
    for (p = str_znext(&s->names, NULL); (p != NULL) && (strcmp(p, sub) != 0);
         p = str_znext(&s->names, p)) {
        i++;
    }
    if (p == NULL) {
        if (s->n == s->cap) {
            s->cap = (s->cap == 0U) ? 8U : (2U * s->cap);
            s->ids = xrealloc(s->ids, s->cap * sizeof *s->ids);
            s->paths = xrealloc(s->paths, s->cap * sizeof *s->paths);
        }
        str_init(&s->ids[s->n]);
        str_init(&s->paths[s->n]);
        str_addz(&s->names, sub);
        i = s->n++;
    }
    if (strcmp(key, "id") == 0) {
        str_reset(&s->ids[i]);
        str_add(&s->ids[i], val);
    } else if (strcmp(key, "path") == 0) {
        str_reset(&s->paths[i]);
        str_add(&s->paths[i], val);
    } else {
        /* branch (default), oldpath: non servono qui */
    }
}

/* Path relativo, con '/', senza elementi vuoti, "." o "..". */
static int path_ok(const char *p)
{
    const char *seg = p;
    size_t len;
    int ok = (*p != '\0') && (*p != '/') && (strchr(p, '\\') == NULL) &&
             (strchr(p, ':') == NULL);

    while (ok && (seg != NULL)) {
        len = (strchr(seg, '/') != NULL) ? (size_t)(strchr(seg, '/') - seg) : strlen(seg);
        ok = (len > 0U) && !((len == 1U) && (seg[0] == '.')) &&
             !((len == 2U) && (seg[0] == '.') && (seg[1] == '.'));
        seg = strchr(seg, '/');
        if (seg != NULL) {
            seg++;
        }
    }
    return ok;
}

/* ---- ref del peer ------------------------------------------------------ */

/* Ref del peer: file sciolto, poi packed-refs. 1 trovata, 0 no, -1 non si
 * capisce (ref simbolica, valore malformato). */
static int ref_lookup(const FpPeer_t *pp, const char *ref, size_t hexlen,
                      Oid_t *oid)
{
    Str_t path = STR_INIT;
    Str_t text = STR_INIT;
    const char *line;
    const char *nl;
    const size_t rlen = strlen(ref);
    int rc = 0;

    str_addf(&path, "%s/%s", str_cstr(&pp->git_dir), ref);
    if (read_file(str_cstr(&path), &text)) {
        str_trim_eol(&text);
        rc = ((text.len == hexlen) && oid_from_hex(oid, text.buf, hexlen)) ? 1 : -1;
        goto exit;
    }
    str_reset(&path);
    str_addf(&path, "%s/packed-refs", str_cstr(&pp->git_dir));
    if (!read_file(str_cstr(&path), &text)) {
        goto exit;
    }
    for (line = str_cstr(&text); (line != NULL) && (*line != '\0') && (rc == 0);
         line = (nl != NULL) ? (nl + 1) : NULL) {
        nl = strchr(line, '\n');
        if ((((nl != NULL) ? (size_t)(nl - line) : strlen(line)) >= (hexlen + 1U + rlen)) &&
            (line[0] != '#') && (line[0] != '^') && (line[hexlen] == ' ') &&
            (strncmp(line + hexlen + 1U, ref, rlen) == 0) &&
            ((line[hexlen + 1U + rlen] == '\n') || (line[hexlen + 1U + rlen] == '\r') ||
             (line[hexlen + 1U + rlen] == '\0'))) {
            rc = oid_from_hex(oid, line, hexlen) ? 1 : -1;
        }
    }

exit:
    str_free(&text);
    str_free(&path);
    return rc;
}

/* Un'operazione di git a meta' (merge, rebase, cherry-pick, revert, am). */
static int peer_busy(const FpPeer_t *pp)
{
    static const char *const marks[] = { "MERGE_HEAD", "CHERRY_PICK_HEAD",
                                         "REVERT_HEAD", "rebase-merge",
                                         "rebase-apply" };
    Str_t path = STR_INIT;
    size_t i;
    int busy = 0;

    for (i = 0U; (i < (sizeof marks / sizeof marks[0])) && !busy; i++) {
        str_reset(&path);
        str_addf(&path, "%s/%s", str_cstr(&pp->git_dir), marks[i]);
        busy = file_exists(str_cstr(&path));
    }
    str_free(&path);
    return busy;
}

/* ---- classificazione --------------------------------------------------- */

/* Esito di un file, con lo stato del peer gia' letto. */
static eFpKind_t classify_file(FpReader_t *rd, FpPeer_t *pp, FpFile_t *f,
                               FpChain_t *c, const Oid_t *head_tree,
                               size_t hexlen)
{
    Str_t ref = STR_INIT;
    Oid_t tmp;
    size_t ip;
    size_t i;
    eFpKind_t kind = FP_FALLBACK;
    int in_chain = 0;

    if (!c->ok) {
        goto exit;
    }
    str_addf(&ref, "refs/shared-merge/%s", c->id);
    if (ref_lookup(pp, str_cstr(&ref), hexlen, &tmp) != 0) {
        goto exit; /* unione in corso: la decide chi ci lavora */
    }
    str_reset(&ref);
    str_addf(&ref, "refs/shared/%s", c->id);
    if ((ref_lookup(pp, str_cstr(&ref), hexlen, &f->tp) != 1) ||
        !tree_lookup(rd, head_tree, str_cstr(&f->path), &f->head)) {
        goto exit;
    }
    ip = chain_find(c, &f->tp);
    if (ip == c->n) {
        goto exit; /* il peer ha una versione che qui manca */
    }
    chain_mark(c, ip);
    /* tra le due versioni l'autorita' non cambia (D-111, D-114) */
    for (i = 0U; i < c->n; i++) {
        if ((c->v[i].mark == 0U) &&
            (!entry_eq(&c->v[i].creator, &c->v[ip].creator) ||
             !entry_eq(&c->v[i].sovereigns, &c->v[ip].sovereigns) ||
             !entry_eq(&c->v[i].heirs, &c->v[ip].heirs))) {
            goto exit;
        }
    }
    for (i = 0U; (i < c->n) && !in_chain; i++) {
        in_chain = oid_eq(&c->v[i].content, &f->head);
    }
    if (oid_eq(&f->head, &c->v[0].content)) {
        kind = (ip == 0U) ? FP_ALIGNED : FP_REF_ONLY;
    } else if (in_chain) {
        kind = FP_APPLY; /* il file e' una versione della catena, non una
                          * modifica: e' "da applicare" */
    } else {
        /* modificato e committato: divergente o da pubblicare */
    }

exit:
    str_free(&ref);
    return kind;
}

void fp_classify(FpReader_t *rd, FpPeer_t *pp, FpChain_t *chains,
                 const char *objfmt)
{
    PeerCfg_t cfg;
    PeerShared_t sh;
    Str_t path = STR_INIT;
    Str_t text = STR_INIT;
    Str_t branch = STR_INIT;
    Oid_t head;
    Oid_t tree;
    Oid_t parents[2];
    unsigned np;
    size_t hexlen = 0U;
    size_t i;
    size_t k;
    size_t hit;
    int fast = 0;

    memset(&cfg, 0, sizeof cfg);
    memset(&sh, 0, sizeof sh);
    if (pp->kind != FPP_FAST) {
        return;
    }
    for (i = 0U; i < pp->n; i++) {
        if (chains[pp->f[i].chain].t.len > 0U) {
            hexlen = 2U * chains[pp->f[i].chain].t.len;
        }
    }
    cfg.pp = pp;
    cfg.chains = chains;
    cfg.in_config = zalloc(pp->n, 1U);
    cfg.pending = zalloc(pp->n, 1U);
    cfg.branches = zalloc(pp->n, sizeof *cfg.branches);
    str_init(&cfg.objfmt);
    sh.pp = pp;
    sh.chains = chains;
    str_init(&sh.names);

    /* config, reftable, formato degli oggetti */
    str_addf(&path, "%s/reftable", str_cstr(&pp->git_dir));
    if ((hexlen == 0U) || sys_is_dir(str_cstr(&path))) {
        goto exit;
    }
    str_reset(&path);
    str_addf(&path, "%s/config", str_cstr(&pp->git_dir));
    if (!read_file(str_cstr(&path), &text) || !cfg_parse(&text, peer_cfg_fn, &cfg) ||
        (cfg.bad != 0)) {
        goto exit;
    }
    if (cfg.off != 0) {
        pp->kind = FPP_OFF; /* come il figlio: prima di tutto il resto */
        out_line(pp, MSG_UPD_PEER_OFF, "");
        goto done;
    }
    if (strcmp((cfg.objfmt.len > 0U) ? str_cstr(&cfg.objfmt) : "sha1", objfmt) != 0) {
        goto exit;
    }

    /* HEAD su un branch, nessuna operazione a meta', commit di HEAD */
    str_reset(&path);
    str_addf(&path, "%s/HEAD", str_cstr(&pp->git_dir));
    if (!read_file(str_cstr(&path), &text)) {
        goto exit;
    }
    str_trim_eol(&text);
    if ((strncmp(str_cstr(&text), "ref: refs/heads/", 16U) != 0) || peer_busy(pp)) {
        goto exit; /* HEAD staccata o merge in corso: lo dice il figlio */
    }
    str_add(&branch, text.buf + 5);
    if ((ref_lookup(pp, str_cstr(&branch), hexlen, &head) != 1) ||
        (rd_get(rd, &head, &text) != GIT_OBJ_COMMIT) ||
        !parse_commit(&text, hexlen, &tree, parents, &np)) {
        goto exit;
    }

    /* .gitshared del working tree, come lo legge lo stato */
    str_reset(&path);
    str_addf(&path, "%s/.gitshared", str_cstr(&pp->root));
    if (!read_file(str_cstr(&path), &text) || !cfg_parse(&text, peer_shared_fn, &sh)) {
        goto exit;
    }

    for (i = 0U; i < pp->n; i++) {
        hit = sh.n;
        for (k = 0U; k < sh.n; k++) {
            if (str_eq(&sh.ids[k], chains[pp->f[i].chain].id)) {
                if (hit != sh.n) {
                    goto exit; /* due sezioni con lo stesso ID */
                }
                hit = k;
            }
        }
        if ((hit == sh.n) || !path_ok(str_cstr(&sh.paths[hit])) ||
            (cfg.in_config[i] == 0U) || (cfg.pending[i] != 0U) ||
            !str_zhas(&cfg.branches[i], branch.buf + 11)) {
            goto exit; /* non collegato, in attesa, sospeso */
        }
        str_reset(&pp->f[i].path);
        str_add(&pp->f[i].path, str_cstr(&sh.paths[hit]));
        pp->f[i].kind = (unsigned char)classify_file(rd, pp, &pp->f[i],
                                                     &chains[pp->f[i].chain],
                                                     &tree, hexlen);
        if (pp->f[i].kind == FP_FALLBACK) {
            goto exit;
        }
    }
    fast = 1;

exit:
    if (!fast) {
        to_fallback(pp);
    }
done:
    for (i = 0U; i < pp->n; i++) {
        str_free(&cfg.branches[i]);
    }
    for (k = 0U; k < sh.n; k++) {
        str_free(&sh.ids[k]);
        str_free(&sh.paths[k]);
    }
    free(sh.ids);
    free(sh.paths);
    str_free(&sh.names);
    free(cfg.branches);
    free(cfg.pending);
    free(cfg.in_config);
    str_free(&cfg.objfmt);
    str_free(&branch);
    str_free(&text);
    str_free(&path);
}

/* ---- scrittura nel peer ------------------------------------------------ */

/* zlib con blocchi non compressi (RFC 1950/1951): git li legge come ogni
 * oggetto sciolto, e non serve una libreria. */
static int zlib_stored(FILE *f, const Str_t *data)
{
    const unsigned char *p = (const unsigned char *)str_cstr(data);
    size_t left = data->len;
    size_t n;
    size_t i;
    unsigned long a = 1UL;
    unsigned long b = 0UL;
    unsigned char hdr[5];
    unsigned char tail[4];
    int ok = (fputc(0x78, f) != EOF) && (fputc(0x01, f) != EOF);

    do {
        n = (left > 65535U) ? 65535U : left;
        hdr[0] = (unsigned char)((left == n) ? 1U : 0U); /* ultimo blocco */
        hdr[1] = (unsigned char)(n & 0xFFU);
        hdr[2] = (unsigned char)((n >> 8U) & 0xFFU);
        hdr[3] = (unsigned char)(~n & 0xFFU);
        hdr[4] = (unsigned char)((~n >> 8U) & 0xFFU);
        ok = ok && (fwrite(hdr, 1U, sizeof hdr, f) == sizeof hdr) &&
             ((n == 0U) || (fwrite(p, 1U, n, f) == n));
        for (i = 0U; i < n; i++) {
            a = (a + p[i]) % 65521UL;
            b = (b + a) % 65521UL;
        }
        p += n;
        left -= n;
    } while (ok && (left > 0U));
    tail[0] = (unsigned char)((b >> 8U) & 0xFFU);
    tail[1] = (unsigned char)(b & 0xFFU);
    tail[2] = (unsigned char)((a >> 8U) & 0xFFU);
    tail[3] = (unsigned char)(a & 0xFFU);
    return ok && (fwrite(tail, 1U, sizeof tail, f) == sizeof tail);
}

/* Oggetto sciolto nel peer: se c'e' gia' non si riscrive. */
static int obj_write(const FpPeer_t *pp, const char *type, const Oid_t *oid,
                     const Str_t *content)
{
    char hex[OID_HEX_SIZE];
    uint8_t rnd[6];
    Str_t dir = STR_INIT;
    Str_t path = STR_INIT;
    Str_t tmp = STR_INIT;
    Str_t data = STR_INIT;
    FILE *f;
    size_t i;
    int ok = 0;

    oid_to_hex(oid, hex);
    str_addf(&dir, "%s/objects/%.2s", str_cstr(&pp->git_dir), hex);
    str_addf(&path, "%s/%s", str_cstr(&dir), hex + 2);
    if (file_exists(str_cstr(&path))) {
        ok = 1;
        goto exit;
    }
    if ((sys_mkdir(str_cstr(&dir)) != 0) || (sys_random(rnd, sizeof rnd) != GS_OK)) {
        goto exit;
    }
    str_addf(&tmp, "%s/tmp_obj_", str_cstr(&dir));
    for (i = 0U; i < sizeof rnd; i++) {
        str_addf(&tmp, "%02x", (unsigned)rnd[i]);
    }
    f = sys_fopen_excl(str_cstr(&tmp));
    if (f == NULL) {
        goto exit;
    }
    str_addf(&data, "%s %lu", type, (unsigned long)content->len);
    str_addch(&data, '\0');
    str_addn(&data, str_cstr(content), content->len);
    ok = zlib_stored(f, &data);
    ok = (fclose(f) == 0) && ok;
    if (ok && (sys_rename(str_cstr(&tmp), str_cstr(&path)) != 0)) {
        ok = file_exists(str_cstr(&path)); /* scritto nel frattempo da altri */
    }
    if (!ok || file_exists(str_cstr(&tmp))) {
        (void)sys_remove(str_cstr(&tmp));
    }

exit:
    str_free(&data);
    str_free(&tmp);
    str_free(&path);
    str_free(&dir);
    return ok;
}

/* Copia nel peer un oggetto letto dal lettore, del tipo atteso. */
static int obj_copy(FpReader_t *rd, const FpPeer_t *pp, const Oid_t *oid,
                    eGitObj_t want, Str_t *buf)
{
    static const char *const names[] = { "", "blob", "tree", "commit" };

    return (oid->len == 0U) ||
           ((rd_get(rd, oid, buf) == want) && obj_write(pp, names[want], oid, buf));
}

/* Le versioni che il peer non ha (non raggiungibili dalla sua T). */
static int copy_versions(FpReader_t *rd, const FpPeer_t *pp, FpChain_t *c,
                         const Oid_t *tp)
{
    Str_t buf = STR_INIT;
    size_t ip = chain_find(c, tp);
    size_t i;
    int ok = (ip < c->n);

    if (ok) {
        chain_mark(c, ip);
    }
    /* dalle piu' vecchie: genitori prima dei figli, blob e tree prima del
     * commit */
    for (i = c->n; ok && (i > 0U); i--) {
        if (c->v[i - 1U].mark == 0U) {
            ok = obj_copy(rd, pp, &c->v[i - 1U].content, GIT_OBJ_BLOB, &buf) &&
                 obj_copy(rd, pp, &c->v[i - 1U].creator, GIT_OBJ_BLOB, &buf) &&
                 obj_copy(rd, pp, &c->v[i - 1U].sovereigns, GIT_OBJ_BLOB, &buf) &&
                 obj_copy(rd, pp, &c->v[i - 1U].heirs, GIT_OBJ_BLOB, &buf) &&
                 obj_copy(rd, pp, &c->v[i - 1U].tree, GIT_OBJ_TREE, &buf) &&
                 obj_copy(rd, pp, &c->v[i - 1U].commit, GIT_OBJ_COMMIT, &buf);
        }
    }
    str_free(&buf);
    return ok;
}

/* refs/shared/<id> da old a new, con il lock di git: sotto lock la ref
 * deve essere ancora old, altrimenti nulla cambia. */
static int ref_write(const FpPeer_t *pp, const char *id, const Oid_t *old,
                     const Oid_t *new_oid)
{
    char hex[OID_HEX_SIZE];
    Str_t dir = STR_INIT;
    Str_t ref = STR_INIT;
    Str_t path = STR_INIT;
    Str_t lock = STR_INIT;
    Oid_t cur;
    FILE *f;
    int ok = 0;

    str_addf(&dir, "%s/refs/shared", str_cstr(&pp->git_dir));
    str_addf(&ref, "refs/shared/%s", id);
    str_addf(&path, "%s/%s", str_cstr(&pp->git_dir), str_cstr(&ref));
    str_addf(&lock, "%s.lock", str_cstr(&path));
    if (sys_mkdir(str_cstr(&dir)) != 0) {
        goto exit;
    }
    f = sys_fopen_excl(str_cstr(&lock));
    if (f == NULL) {
        goto exit; /* qualcun altro la sta aggiornando */
    }
    oid_to_hex(new_oid, hex);
    ok = (ref_lookup(pp, str_cstr(&ref), 2U * old->len, &cur) == 1) &&
         oid_eq(&cur, old) && (fprintf(f, "%s\n", hex) > 0);
    ok = (fclose(f) == 0) && ok;
    ok = ok && (sys_rename(str_cstr(&lock), str_cstr(&path)) == 0);
    if (!ok) {
        (void)sys_remove(str_cstr(&lock));
    }

exit:
    str_free(&lock);
    str_free(&path);
    str_free(&ref);
    str_free(&dir);
    return ok;
}

/* Toglie l'avviso di T: il peer ce l'ha (D-099). */
static void drop_notice(const FpPeer_t *pp, const char *id, const Oid_t *t)
{
    char hex[OID_HEX_SIZE];
    Str_t path = STR_INIT;

    oid_to_hex(t, hex);
    str_addf(&path, "%s/shared/inbox/%s/%s", str_cstr(&pp->git_dir), id, hex);
    (void)sys_remove(str_cstr(&path));
    str_free(&path);
}

/* Un file del tree sintetico: path relativo al livello, blob. */
typedef struct {
    const char *path;
    const Oid_t *blob;
} TreeItem_t;

/* Un nome del livello: file o cartella. */
typedef struct {
    const char *name;
    size_t len;
    int dir;
    const Oid_t *blob;
} TreeName_t;

/* Ordine dei nomi in un tree di git: una cartella vale come "<nome>/". */
static int tree_name_cmp(const TreeName_t *a, const TreeName_t *b)
{
    const size_t m = (a->len < b->len) ? a->len : b->len;
    const int r = memcmp(a->name, b->name, m);
    int ca;
    int cb;

    if (r != 0) {
        return r;
    }
    ca = (a->len > m) ? (unsigned char)a->name[m] : (a->dir ? '/' : 0);
    cb = (b->len > m) ? (unsigned char)b->name[m] : (b->dir ? '/' : 0);
    return ca - cb;
}

/*
 * Scrive nel peer il tree con i file dati (path relativi a questo livello,
 * modo 100644) e le sue cartelle; out = il suo oid, calcolato come git:
 * hash di "tree <dimensione>\0<contenuto>". 0 se un nome e' sia file sia
 * cartella, o un oggetto non si scrive.
 */
static int build_tree(const FpPeer_t *pp, const TreeItem_t *items, size_t n,
                      unsigned rawlen, Oid_t *out)
{
    TreeName_t *names = xmalloc(((n > 0U) ? n : 1U) * sizeof *names);
    TreeItem_t *sub = xmalloc(((n > 0U) ? n : 1U) * sizeof *sub);
    TreeName_t tmp;
    Str_t content = STR_INIT;
    Str_t hdr = STR_INIT;
    Oid_t child;
    Sha_t sha;
    const char *slash;
    size_t nn = 0U;
    size_t ns;
    size_t i;
    size_t j;
    int dir;
    int ok = 1;

    for (i = 0U; ok && (i < n); i++) {
        slash = strchr(items[i].path, '/');
        dir = (slash != NULL);
        tmp.name = items[i].path;
        tmp.len = dir ? (size_t)(slash - items[i].path) : strlen(items[i].path);
        tmp.dir = dir;
        tmp.blob = items[i].blob;
        for (j = 0U; (j < nn) && !((names[j].len == tmp.len) &&
                                   (memcmp(names[j].name, tmp.name, tmp.len) == 0));
             j++) {
        }
        if (j < nn) {
            ok = dir && names[j].dir; /* la stessa cartella, altri file */
        } else {
            names[nn++] = tmp;
        }
    }
    /* ordinamento per inserzione: pochi nomi */
    for (i = 1U; ok && (i < nn); i++) {
        tmp = names[i];
        for (j = i; (j > 0U) && (tree_name_cmp(&names[j - 1U], &tmp) > 0); j--) {
            names[j] = names[j - 1U];
        }
        names[j] = tmp;
    }
    for (i = 0U; ok && (i < nn); i++) {
        if (names[i].dir) {
            ns = 0U;
            for (j = 0U; j < n; j++) {
                if ((strncmp(items[j].path, names[i].name, names[i].len) == 0) &&
                    (items[j].path[names[i].len] == '/')) {
                    sub[ns].path = items[j].path + names[i].len + 1U;
                    sub[ns].blob = items[j].blob;
                    ns++;
                }
            }
            ok = build_tree(pp, sub, ns, rawlen, &child);
            str_add(&content, "40000 ");
        } else {
            child = *names[i].blob;
            str_add(&content, "100644 ");
        }
        str_addn(&content, names[i].name, names[i].len);
        str_addch(&content, '\0');
        str_addn(&content, (const char *)child.raw, rawlen);
    }
    if (ok) {
        str_addf(&hdr, "tree %lu", (unsigned long)content.len);
        sha_init(&sha, rawlen);
        sha_update(&sha, str_cstr(&hdr), hdr.len + 1U); /* con il '\0' */
        sha_update(&sha, str_cstr(&content), content.len);
        sha_final(&sha, out->raw);
        out->len = (uint8_t)rawlen;
        ok = obj_write(pp, "tree", out, &content);
    }
    str_free(&hdr);
    str_free(&content);
    free(sub);
    free(names);
    return ok;
}

/* Il tree sintetico del peer con i file da applicare: "--source=<oid>". */
static int synthetic_tree(FpPeer_t *pp, const FpChain_t *chains)
{
    TreeItem_t *items = xmalloc(((pp->n > 0U) ? pp->n : 1U) * sizeof *items);
    char hex[OID_HEX_SIZE];
    Oid_t tree;
    size_t n = 0U;
    size_t i;
    unsigned rawlen = 0U;
    int ok;

    for (i = 0U; i < pp->n; i++) {
        if (pp->f[i].kind == FP_APPLY) {
            items[n].path = str_cstr(&pp->f[i].path);
            items[n].blob = &chains[pp->f[i].chain].v[0].content;
            rawlen = items[n].blob->len;
            n++;
        }
    }
    ok = (n > 0U) && build_tree(pp, items, n, rawlen, &tree);
    if (ok) {
        oid_to_hex(&tree, hex);
        str_reset(&pp->src);
        str_addf(&pp->src, "--source=%s", hex);
    }
    free(items);
    return ok;
}

/* ---- fasi -------------------------------------------------------------- */

static int has_kind(const FpPeer_t *pp, eFpKind_t kind)
{
    size_t i;
    int has = 0;

    for (i = 0U; (i < pp->n) && !has; i++) {
        has = (pp->f[i].kind == kind);
    }
    return has;
}

/* Avvia "git <pre...> -C <root> <cmd...> -- <path dei file kind>". */
static void phase_start(FpPeer_t *pp, const char *const pre[], size_t npre,
                        const char *const cmd[], size_t ncmd, eFpKind_t kind,
                        const char *extra)
{
    const char **args = xmalloc((npre + ncmd + pp->n + 6U) * sizeof *args);
    size_t a = 0U;
    size_t i;

    for (i = 0U; i < npre; i++) {
        args[a++] = pre[i];
    }
    args[a++] = "-C";
    args[a++] = str_cstr(&pp->root);
    for (i = 0U; i < ncmd; i++) {
        args[a++] = cmd[i];
    }
    args[a++] = "--";
    for (i = 0U; i < pp->n; i++) {
        if (pp->f[i].kind == kind) {
            args[a++] = str_cstr(&pp->f[i].path);
        }
    }
    if (extra != NULL) {
        args[a++] = extra;
    }
    args[a] = NULL;
    pp->started = (git_start(&pp->p, args, PROC_CLEAN_ENV | PROC_STDERR_OUT) == GS_OK);
    free((void *)args);
}

/* Raccoglie la fase: 1 se il comando e' riuscito; out = la sua uscita. */
static int phase_finish(FpPeer_t *pp, Str_t *out)
{
    int code = -1;

    str_reset(out);
    if (pp->started && (proc_collect(&pp->p, out, &code) != GS_OK)) {
        code = -1;
    }
    pp->started = 0;
    return code == 0;
}

/* I file kind di un peer diventano FP_FAILED, con il messaggio e l'uscita
 * di git. */
static void fail_files(FpPeer_t *pp, eFpKind_t kind, eMsgId_t id, const Str_t *gitout)
{
    size_t i;

    for (i = 0U; i < pp->n; i++) {
        if (pp->f[i].kind == kind) {
            pp->f[i].kind = FP_FAILED;
            out_line(pp, id, str_cstr(&pp->f[i].path));
        }
    }
    if ((gitout != NULL) && (gitout->len > 0U)) {
        str_add(&pp->out, str_cstr(gitout));
        if (gitout->buf[gitout->len - 1U] != '\n') {
            str_addch(&pp->out, '\n');
        }
    }
    pp->rc = 1;
}

void fp_status_start(FpPeer_t *peers, size_t n)
{
    static const char *const pre[] = { "--no-optional-locks", "--literal-pathspecs" };
    static const char *const cmd[] = { "status", "--porcelain=v2", "-z",
                                       "--untracked-files=no",
                                       "--ignore-submodules=all" };
    size_t k;

    for (k = 0U; k < n; k++) {
        if ((peers[k].kind == FPP_FAST) && has_kind(&peers[k], FP_APPLY)) {
            phase_start(&peers[k], pre, 2U, cmd, 5U, FP_APPLY, ".gitshared");
        }
    }
}

/* Path di un record di "status --porcelain=v2 -z" ("" per le intestazioni);
 * *skip = 1 se il record dopo e' il path di prima di una rinomina. */
static const char *status_path(const char *rec, int *skip)
{
    unsigned fields = 0U;
    const char *p = rec;

    *skip = 0;
    if (rec[0] == '1') {
        fields = 8U;
    } else if (rec[0] == '2') {
        fields = 9U;
        *skip = 1;
    } else if (rec[0] == 'u') {
        fields = 10U;
    } else if ((rec[0] == '?') || (rec[0] == '!')) {
        fields = 1U;
    } else {
        p = ""; /* "#": intestazione */
    }
    while ((fields > 0U) && (p != NULL) && (*p != '\0')) {
        p = strchr(p, ' ');
        if (p != NULL) {
            p++;
        }
        fields--;
    }
    return (p != NULL) ? p : "";
}

void fp_status_finish(FpPeer_t *peers, size_t n)
{
    Str_t out = STR_INIT;
    const char *rec;
    const char *end;
    const char *path;
    size_t k;
    size_t i;
    int skip;
    int next_skip = 0;

    for (k = 0U; k < n; k++) {
        if ((peers[k].kind != FPP_FAST) || !peers[k].started) {
            continue;
        }
        if (!phase_finish(&peers[k], &out)) {
            to_fallback(&peers[k]);
            continue;
        }
        end = str_cstr(&out) + out.len;
        for (rec = str_cstr(&out); (rec < end) && (peers[k].kind == FPP_FAST);
             rec += strlen(rec) + 1U) {
            path = next_skip ? rec : status_path(rec, &skip);
            if (next_skip) {
                skip = 0;
            }
            next_skip = skip;
            if (strcmp(path, ".gitshared") == 0) {
                to_fallback(&peers[k]); /* lo stato lo ricava il figlio */
            }
            for (i = 0U; (i < peers[k].n) && (peers[k].kind == FPP_FAST); i++) {
                if ((peers[k].f[i].kind == FP_APPLY) && str_eq(&peers[k].f[i].path, path)) {
                    peers[k].f[i].kind = FP_LOCAL;
                }
            }
        }
        next_skip = 0;
    }
    str_free(&out);
}

/* Oggetti e ref di un file, se il peer non ha ancora T. */
static void write_file_chain(FpReader_t *rd, FpPeer_t *pp, FpFile_t *f, FpChain_t *c)
{
    if (oid_eq(&f->tp, &c->t)) {
        return;
    }
    if (copy_versions(rd, pp, c, &f->tp) && ref_write(pp, c->id, &f->tp, &c->t)) {
        drop_notice(pp, c->id, &c->t);
        f->tp = c->t;
    } else {
        f->kind = FP_FAILED;
        out_line(pp, MSG_UPD_PEER_WRITE_FAILED, str_cstr(&f->path));
        pp->rc = 1;
    }
}

void fp_apply(FpReader_t *rd, FpPeer_t *peers, size_t n, FpChain_t *chains,
              const char *message)
{
    const char *restore_pre[] = { "--literal-pathspecs", "-c", NULL };
    const char *restore_cmd[] = { "restore", NULL, "--staged", "--worktree" };
    static const char *const commit_pre[] = { "--literal-pathspecs", "-c",
                                              "maintenance.auto=false" };
    const char *commit_cmd[] = { "commit", "-q", "-m", NULL, "--only" };
    Str_t gitout = STR_INIT;
    Str_t text = STR_INIT;
    Str_t hooks = STR_INIT;
    FpPeer_t *pp;
    size_t k;
    size_t i;
    size_t a;
    unsigned napply;

    /* 1. oggetti e ref, nel padre */
    for (k = 0U; k < n; k++) {
        pp = &peers[k];
        for (i = 0U; (pp->kind == FPP_FAST) && (i < pp->n); i++) {
            if ((pp->f[i].kind == FP_REF_ONLY) || (pp->f[i].kind == FP_APPLY) ||
                (pp->f[i].kind == FP_LOCAL)) {
                write_file_chain(rd, pp, &pp->f[i], &chains[pp->f[i].chain]);
            }
        }
    }

    /* 2. tree sintetico con i soli file da applicare, nel padre; poi
     * git restore: file e index insieme, con i filtri e i fine riga del
     * peer (un processo invece di update-index + checkout-index) */
    for (k = 0U; k < n; k++) {
        pp = &peers[k];
        if ((pp->kind != FPP_FAST) || !has_kind(pp, FP_APPLY)) {
            continue;
        }
        if (!synthetic_tree(pp, chains)) {
            fail_files(pp, FP_APPLY, MSG_UPD_PEER_WRITE_FAILED, NULL);
            continue;
        }
        /* restore lancerebbe post-checkout (anche il blocco di git shared:
         * sh e un git-shared in piu' per peer); checkout-index, che faceva
         * lo stesso lavoro, non lancia hook. Una cartella degli hook che non
         * esiste: nessun hook. */
        str_reset(&hooks);
        str_addf(&hooks, "core.hooksPath=%s/shared/no-hooks", str_cstr(&pp->git_dir));
        restore_pre[2] = str_cstr(&hooks);
        restore_cmd[1] = str_cstr(&pp->src);
        phase_start(pp, restore_pre, 3U, restore_cmd, 4U, FP_APPLY, NULL);
    }
    for (k = 0U; k < n; k++) {
        if ((peers[k].kind == FPP_FAST) && peers[k].started &&
            !phase_finish(&peers[k], &gitout)) {
            fail_files(&peers[k], FP_APPLY, MSG_UPD_PEER_WRITE_FAILED, &gitout);
        }
    }

    /* 4. commit dei soli file applicati (D-075), con gli hook del peer */
    for (k = 0U; k < n; k++) {
        pp = &peers[k];
        if ((pp->kind != FPP_FAST) || !has_kind(pp, FP_APPLY)) {
            continue;
        }
        napply = 0U;
        for (i = 0U; i < pp->n; i++) {
            if (pp->f[i].kind == FP_APPLY) {
                napply++;
                a = i;
            }
        }
        str_reset(&text);
        if (message != NULL) {
            str_add(&text, message);
        } else {
            str_addf(&text, msg(MSG_UPD_COMMIT_MSG),
                     (napply == 1U) ? str_cstr(&pp->f[a].path) : "shared files");
        }
        commit_cmd[3] = str_cstr(&text);
        phase_start(pp, commit_pre, 3U, commit_cmd, 5U, FP_APPLY, NULL);
    }
    for (k = 0U; k < n; k++) {
        pp = &peers[k];
        if ((pp->kind != FPP_FAST) || !pp->started) {
            continue;
        }
        if (phase_finish(pp, &gitout)) {
            for (i = 0U; i < pp->n; i++) {
                if (pp->f[i].kind == FP_APPLY) {
                    pp->f[i].kind = FP_DONE;
                }
            }
        } else {
            fail_files(pp, FP_APPLY, MSG_UPD_PEER_COMMIT_FAILED, &gitout);
        }
    }

    /* 5. le righe per l'utente, nell'ordine dei file */
    for (k = 0U; k < n; k++) {
        pp = &peers[k];
        for (i = 0U; (pp->kind == FPP_FAST) && (i < pp->n); i++) {
            if ((pp->f[i].kind == FP_ALIGNED) || (pp->f[i].kind == FP_REF_ONLY)) {
                out_line(pp, MSG_UPD_UP_TO_DATE, str_cstr(&pp->f[i].path));
            } else if (pp->f[i].kind == FP_DONE) {
                out_line(pp, MSG_UPD_COMMITTED, str_cstr(&pp->f[i].path));
            } else if (pp->f[i].kind == FP_LOCAL) {
                out_line(pp, MSG_UPD_PEER_LOCAL, str_cstr(&pp->f[i].path));
                pp->rc = 1;
            } else {
                /* FP_FAILED: la riga c'e' gia' */
            }
        }
    }
    str_free(&hooks);
    str_free(&text);
    str_free(&gitout);
}
