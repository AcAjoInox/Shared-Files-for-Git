/*
 * cmd_branch.c - branch attivi di un file shared (REQ-F-022, D-083).
 *
 *   git shared branch [<path>]                        mostra gli elenchi
 *   git shared branch <path> (-a | -rm) <b>           elenco locale
 *                            (forme lunghe: --add, --remove)
 *   git shared branch <path> --reset                  locale = default
 *   git shared branch <path> --default (--add | --remove) <b> [-c | -m <msg>]
 *                                                     default in .gitshared
 *
 * L'elenco locale (.git/config) ha effetto subito: e' un dato locale, non
 * versionato (REQ-F-023). Il default sta in .gitshared: si mette in
 * staging e conta al commit; e' quello che set, link e i cloni copiano
 * (D-057). Nella v0 solo nomi esatti: i pattern (D-056) sono rifiutati.
 */
#include <stdio.h>
#include <string.h>

#include "cmd.h"
#include "cmd_common.h"

/* Opzioni del comando: le azioni stanno in `ops`, elenco compatto di
 * "+<branch>" (aggiunge) e "-<branch>" (toglie), nell'ordine dato. */
typedef struct {
    const char *path;
    const char *message;
    Str_t ops;
    unsigned reset : 1U;
    unsigned dflt : 1U;
    unsigned commit : 1U;
} BranchOpts_t;

/* 1 se riesce; altrimenti errore e uso gia' stampati. */
static int parse_args(int argc, char **argv, BranchOpts_t *o)
{
    const char *a;
    int i;
    int end_opts = 0;
    int ok = 1;

    for (i = 1; (i < argc) && ok; i++) {
        a = argv[i];
        if (!end_opts && (strcmp(a, "--") == 0)) {
            end_opts = 1;
        } else if (!end_opts && ((strcmp(a, "--add") == 0) ||
                                 (strcmp(a, "-a") == 0) ||
                                 (strcmp(a, "--remove") == 0) ||
                                 (strcmp(a, "-rm") == 0))) {
            /* -a e -rm: forme brevi di --add e --remove */
            if ((i + 1) >= argc) {
                msg_error(MSG_ERR_OPT_VALUE, a);
                ok = 0;
            } else {
                str_addch(&o->ops, ((strcmp(a, "--add") == 0) ||
                                    (strcmp(a, "-a") == 0)) ? '+' : '-');
                str_addz(&o->ops, argv[++i]);
            }
        } else if (!end_opts && (strcmp(a, "--reset") == 0)) {
            o->reset = 1U;
        } else if (!end_opts && (strcmp(a, "--default") == 0)) {
            o->dflt = 1U;
        } else if (!end_opts && ((strcmp(a, "-c") == 0) ||
                                 (strcmp(a, "--commit") == 0))) {
            o->commit = 1U;
        } else if (!end_opts && (strcmp(a, "-m") == 0)) {
            if ((i + 1) >= argc) {
                msg_error(MSG_ERR_OPT_VALUE, a);
                ok = 0;
            } else {
                o->message = argv[++i];
                o->commit = 1U;
            }
        } else if (!end_opts && (a[0] == '-') && (a[1] != '\0')) {
            msg_error(MSG_ERR_UNKNOWN_OPT, a);
            ok = 0;
        } else if (o->path != NULL) {
            msg_error(MSG_ERR_TOO_MANY_ARGS);
            ok = 0;
        } else {
            o->path = a;
        }
    }
    if (ok && o->reset && ((o->ops.len > 0U) || o->dflt)) {
        msg_error(MSG_ERR_BRANCH_RESET_ALONE);
        ok = 0;
    } else if (ok && o->dflt && (o->ops.len == 0U)) {
        msg_error(MSG_ERR_BRANCH_DEFAULT_OP);
        ok = 0;
    } else if (ok && o->commit && !o->dflt) {
        msg_error(MSG_ERR_BRANCH_COMMIT);
        ok = 0;
    } else if (ok && ((o->ops.len > 0U) || o->reset) && (o->path == NULL)) {
        msg_error(MSG_ERR_MISSING_PATH);
        ok = 0;
    } else {
        /* combinazione valida */
    }
    if (!ok) {
        fputs(msg(MSG_USAGE_BRANCH), stderr);
    }
    return ok;
}

/* Nome di branch valido (refs/heads/<nome>); i jolly hanno un loro
 * messaggio: i pattern non sono nella v0 (D-056). */
static int name_is_valid(const char *b)
{
    const char *args[] = { "check-ref-format", NULL, NULL };
    Str_t ref = STR_INIT;
    Str_t out = STR_INIT;
    int code = 1;

    str_addf(&ref, "refs/heads/%s", b);
    args[1] = str_cstr(&ref);
    if ((b[0] == '-') || (git_capture(args, PROC_STDERR_NULL, &out, &code) != GS_OK)) {
        code = 1;
    }
    if ((code != 0) && (strpbrk(b, "*?[") != NULL)) {
        msg_error(MSG_ERR_BRANCH_PATTERN, b);
    } else if (code != 0) {
        msg_error(MSG_ERR_BRANCH_NAME, b);
    } else {
        /* valido */
    }
    str_free(&out);
    str_free(&ref);
    return code == 0;
}

/* Branch locali esistenti, nomi brevi, elenco compatto. */
static eGsErr_t local_branches(Str_t *out)
{
    return repo_branch_names(out);
}

/* Elenco di default della voce, dal .gitshared del working tree. */
static eGsErr_t default_list(const Repo_t *r, const char *id, Str_t *out)
{
    Repo_t d = REPO_INIT;
    Str_t file = STR_INIT;
    const SharedEntry_t *e;
    eGsErr_t err;

    str_addf(&file, "%s/.gitshared", str_cstr(&r->top));
    err = repo_load_gitshared(&d, str_cstr(&file), 0, 1);
    e = (err == GS_OK) ? repo_find_id(&d, id) : NULL;
    str_reset(out);
    if (e != NULL) {
        str_addn(out, e->branches.buf, e->branches.len);
    }
    repo_close(&d);
    str_free(&file);
    return err;
}

/*
 * Aggiunge o toglie un valore di una chiave multipla: in .git/config
 * (file = NULL) o nel file dato. *absent = 1 se --remove non ha trovato
 * il valore.
 */
static eGsErr_t list_edit(const char *file, const char *key, int add,
                          const char *value, int *absent)
{
    const char *args[8];
    Str_t out = STR_INIT;
    size_t n = 0U;
    int code = 0;
    eGsErr_t err;

    args[n++] = "config";
    if (file != NULL) {
        args[n++] = "-f";
        args[n++] = file;
    }
    if (add) {
        args[n++] = "--add";
    } else {
        args[n++] = "--fixed-value";
        args[n++] = "--unset-all";
    }
    args[n++] = key;
    args[n++] = value;
    args[n] = NULL;
    err = git_capture(args, PROC_STDERR_NULL, &out, &code);
    *absent = (!add && (code == 5));
    if ((err == GS_OK) && (code != 0) && !*absent) {
        err = GS_ERR_GIT;
    }
    str_free(&out);
    return err;
}

/* Una riga "    <campo>  a, b (current), c (no such branch)". */
static void print_list(eMsgId_t field, const Str_t *list, const Str_t *exist,
                       const char *current)
{
    const char *b;
    int first = 1;

    printf("    %-8s ", msg(field));
    for (b = str_znext(list, NULL); b != NULL; b = str_znext(list, b)) {
        printf("%s%s", first ? "" : ", ", b);
        if (strcmp(b, current) == 0) {
            printf(" (%s)", msg(MSG_BRANCH_CURRENT));
        } else if (!str_zhas(exist, b)) {
            printf(" (%s)", msg(MSG_BRANCH_MISSING));
        } else {
            /* branch esistente */
        }
        first = 0;
    }
    if (first) {
        fputs(msg(MSG_BRANCH_NONE), stdout);
    }
    putchar('\n');
}

/* Mostra gli elenchi: di un file o di tutti. */
static int show(Repo_t *r, const char *path)
{
    Str_t exist = STR_INIT;
    Str_t dflt = STR_INIT;
    const SharedEntry_t *e;
    size_t i;
    size_t shown = 0U;
    int rc = GS_EXIT_OK;

    if (local_branches(&exist) != GS_OK) {
        msg_fatal(MSG_ERR_PROC, "git for-each-ref");
        rc = GS_EXIT_FATAL;
        goto exit;
    }
    for (i = 0U; i < r->n; i++) {
        e = &r->v[i];
        if (!(e->in_gitshared && e->valid) ||
            ((path != NULL) && !str_eq(&e->path, path))) {
            continue;
        }
        if (default_list(r, e->id, &dflt) != GS_OK) {
            msg_fatal(MSG_ERR_READ_GITSHARED);
            rc = GS_EXIT_FATAL;
            goto exit;
        }
        if (shown > 0U) {
            putchar('\n');
        }
        printf("%s\n", str_cstr(&e->path));
        if (!e->in_config) {
            printf("    %-8s %s\n", msg(MSG_BRANCH_FIELD_ACTIVE),
                   msg(MSG_BRANCH_NOT_LINKED));
        } else if (e->pending != SHARED_PENDING_NONE) {
            printf("    %-8s %s\n", msg(MSG_BRANCH_FIELD_ACTIVE),
                   msg(MSG_STATE_PENDING));
        } else {
            print_list(MSG_BRANCH_FIELD_ACTIVE, &e->branches, &exist,
                       str_cstr(&r->branch));
        }
        print_list(MSG_BRANCH_FIELD_DEFAULT, &dflt, &exist, str_cstr(&r->branch));
        shown++;
    }
    if (shown == 0U) {
        puts(msg(MSG_STATUS_NONE));
    }

exit:
    str_free(&dflt);
    str_free(&exist);
    return rc;
}

/* Avvisa se un branch aggiunto non esiste (ancora): non e' un errore. */
static void warn_missing(const Str_t *ops, const Str_t *exist)
{
    const char *op;

    for (op = str_znext(ops, NULL); op != NULL; op = str_znext(ops, op)) {
        if ((op[0] == '+') && !str_zhas(exist, op + 1)) {
            msg_warning(MSG_WARN_BRANCH_MISSING, op + 1);
        }
    }
}

/* Copia di list senza item (elenchi compatti). */
static void zlist_remove(Str_t *list, const char *item)
{
    Str_t out = STR_INIT;
    const char *c;

    for (c = str_znext(list, NULL); c != NULL; c = str_znext(list, c)) {
        if (strcmp(c, item) != 0) {
            str_addz(&out, c);
        }
    }
    str_reset(list);
    str_addn(list, out.buf, out.len);
    str_free(&out);
}

/* --add/--remove sull'elenco locale o sul default. */
static int edit(Repo_t *r, SharedEntry_t *e, const BranchOpts_t *o)
{
    Str_t key = STR_INIT;
    Str_t file = STR_INIT;
    Str_t list = STR_INIT; /* elenco attuale, per non creare doppioni */
    Str_t exist = STR_INIT;
    Str_t text = STR_INIT;
    Str_t paths = STR_INIT;
    const char *label = str_cstr(&e->path);
    const char *op;
    const char *message;
    eMsgId_t done;
    int absent = 0;
    int add;
    int changed = 0;
    int rc = GS_EXIT_ERROR;

    for (op = str_znext(&o->ops, NULL); op != NULL; op = str_znext(&o->ops, op)) {
        if ((op[0] == '+') && !name_is_valid(op + 1)) {
            goto exit;
        }
    }
    if (local_branches(&exist) != GS_OK) {
        msg_fatal(MSG_ERR_PROC, "git for-each-ref");
        rc = GS_EXIT_FATAL;
        goto exit;
    }
    if (o->dflt) {
        str_addf(&file, "%s/.gitshared", str_cstr(&r->top));
        str_addf(&key, "shared.%s.branch", str_cstr(&e->name));
        if (default_list(r, e->id, &list) != GS_OK) {
            msg_error(MSG_ERR_READ_GITSHARED);
            goto exit;
        }
    } else {
        str_addf(&key, "shared.%s.branch", e->id);
        str_addn(&list, e->branches.buf, e->branches.len);
    }

    /* Prima l'elenco finale, in memoria: un --remove di un branch assente
     * o un elenco locale vuoto si rifiutano prima di scrivere. Un elenco
     * locale vuoto non si puo' tenere: la sezione [shared "<id>"] di un
     * file senza peer resterebbe senza chiavi, e per git non esiste piu'
     * (il file risulterebbe scollegato). Per non condividere: unlink. */
    str_addn(&text, list.buf, list.len);
    for (op = str_znext(&o->ops, NULL); op != NULL; op = str_znext(&o->ops, op)) {
        if (op[0] == '+') {
            if (!str_zhas(&text, op + 1)) {
                str_addz(&text, op + 1);
            }
        } else if (!str_zhas(&text, op + 1)) {
            msg_error(MSG_ERR_BRANCH_NOT_IN, label, op + 1);
            goto exit;
        } else {
            zlist_remove(&text, op + 1);
        }
    }
    if (!o->dflt && (text.len == 0U)) {
        msg_error(MSG_ERR_BRANCH_LAST, label);
        goto exit;
    }
    str_reset(&text);

    for (op = str_znext(&o->ops, NULL); op != NULL; op = str_znext(&o->ops, op)) {
        add = (op[0] == '+');
        if (add && str_zhas(&list, op + 1)) {
            /* gia' presente: nessun doppione */
        } else if (list_edit(o->dflt ? str_cstr(&file) : NULL, str_cstr(&key), add,
                             op + 1, &absent) != GS_OK) {
            msg_error(o->dflt ? MSG_ERR_WRITE_GITSHARED : MSG_ERR_BRANCH_WRITE, label);
            goto exit;
        } else if (absent) {
            msg_error(MSG_ERR_BRANCH_NOT_IN, label, op + 1);
            goto exit;
        } else {
            changed = 1;
            if (add) {
                str_addz(&list, op + 1);
            } else {
                zlist_remove(&list, op + 1);
            }
        }
        if (o->dflt) {
            done = add ? MSG_BRANCH_DEFAULT_ADDED : MSG_BRANCH_DEFAULT_REMOVED;
        } else {
            done = add ? MSG_BRANCH_ADDED : MSG_BRANCH_REMOVED;
        }
        printf(msg(done), label, op + 1);
        putchar('\n');
    }
    warn_missing(&o->ops, &exist);

    if (!o->dflt) {
        rc = GS_EXIT_OK; /* effetto immediato */
        goto exit;
    }

    /* Default: .gitshared in staging; commit solo con -c/-m (D-007). */
    if (changed && (cmd_stage(r, ".gitshared") != GS_OK)) {
        msg_error(MSG_ERR_STAGE_GITSHARED);
        goto exit;
    }
    if (!o->commit) {
        if (changed) {
            puts(msg(MSG_BRANCH_DEFAULT_STAGED));
        }
        rc = GS_EXIT_OK;
        goto exit;
    }
    message = o->message;
    if (message == NULL) {
        str_addf(&text, msg(MSG_BRANCH_COMMIT_MSG), label);
        message = str_cstr(&text);
    }
    str_addz(&paths, ".gitshared");
    if (changed && (cmd_commit(r, message, &paths) != GS_OK)) {
        msg_error(MSG_ERR_COMMIT);
        goto exit;
    }
    rc = GS_EXIT_OK;

exit:
    str_free(&paths);
    str_free(&text);
    str_free(&exist);
    str_free(&list);
    str_free(&file);
    str_free(&key);
    return rc;
}

/* --reset: l'elenco locale torna una copia del default in .gitshared; se
 * il default e' vuoto, il branch corrente (come al set, D-057). */
static int reset(Repo_t *r, SharedEntry_t *e)
{
    const char *unset_args[] = { "config", "--unset-all", NULL, NULL };
    Str_t dflt = STR_INIT;
    Str_t key = STR_INIT;
    Str_t out = STR_INIT;
    const char *label = str_cstr(&e->path);
    const char *b;
    int absent = 0;
    int code = 0;
    int rc = GS_EXIT_ERROR;

    if (default_list(r, e->id, &dflt) != GS_OK) {
        msg_error(MSG_ERR_READ_GITSHARED);
        goto exit;
    }
    if (dflt.len == 0U) {
        if (r->branch.len == 0U) {
            msg_error(MSG_ERR_BRANCH_DETACHED, label);
            goto exit;
        }
        str_addz(&dflt, str_cstr(&r->branch));
    }
    str_addf(&key, "shared.%s.branch", e->id);
    unset_args[2] = str_cstr(&key);
    if ((git_capture(unset_args, PROC_STDERR_NULL, &out, &code) != GS_OK) ||
        ((code != 0) && (code != 5))) { /* 5 = elenco gia' vuoto */
        msg_error(MSG_ERR_BRANCH_WRITE, label);
        goto exit;
    }
    for (b = str_znext(&dflt, NULL); b != NULL; b = str_znext(&dflt, b)) {
        if (list_edit(NULL, str_cstr(&key), 1, b, &absent) != GS_OK) {
            msg_error(MSG_ERR_BRANCH_WRITE, label);
            goto exit;
        }
    }
    printf(msg(MSG_BRANCH_RESET), label);
    putchar('\n');
    rc = GS_EXIT_OK;

exit:
    str_free(&out);
    str_free(&key);
    str_free(&dflt);
    return rc;
}

int cmd_branch(int argc, char **argv)
{
    BranchOpts_t o = { NULL, NULL, STR_INIT, 0U, 0U, 0U };
    Repo_t repo = REPO_INIT;
    GitBatch_t batch;
    SharedEntry_t *e = NULL;
    Str_t full = STR_INIT;
    int opened = 0;
    int rc = GS_EXIT_USAGE;

    if (!parse_args(argc, argv, &o)) {
        goto exit;
    }
    rc = cmd_open(&repo, &batch);
    if (rc != GS_EXIT_OK) {
        goto exit;
    }
    opened = 1;
    rc = GS_EXIT_ERROR;

    if (o.path != NULL) {
        if (cmd_repo_path(&repo, o.path, &full)) {
            e = repo_find_path(&repo, str_cstr(&full));
        }
        if ((e == NULL) || !e->valid) {
            msg_error(MSG_ERR_NOT_SHARED_PATH, o.path);
            goto exit;
        }
    }
    if ((o.ops.len == 0U) && !o.reset) {
        rc = show(&repo, (e != NULL) ? str_cstr(&e->path) : NULL);
    } else if (o.dflt) {
        rc = edit(&repo, e, &o); /* .gitshared: anche se non collegato qui */
    } else if (!e->in_config) {
        msg_error(MSG_ERR_NOT_LINKED, str_cstr(&e->path));
    } else if (e->pending != SHARED_PENDING_NONE) {
        msg_error(MSG_ERR_PENDING_OP, str_cstr(&e->path));
    } else if (o.reset) {
        rc = reset(&repo, e);
    } else {
        rc = edit(&repo, e, &o);
    }

exit:
    if (opened != 0) {
        git_batch_close(&batch);
        repo_close(&repo);
    }
    str_free(&full);
    str_free(&o.ops);
    return rc;
}
