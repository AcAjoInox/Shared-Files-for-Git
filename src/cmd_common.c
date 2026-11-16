/*
 * cmd_common.c - parti comuni ai sottocomandi.
 */
#include "cmd_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chain.h"
#include "mem.h"
#include "path.h"
#include "peers.h"
#include "sys.h"

static const char gitshared_header[] =
    "# Managed by 'git shared': do not edit by hand.\n"
    "# This repository shares files with other repositories: "
    "run 'git shared status'.\n";

int cmd_parse(int argc, char **argv, unsigned allowed, eMsgId_t usage,
              CmdOpts_t *o)
{
    int i = 1;
    int end_opts = 0;
    int rc = -1;

    o->message = NULL;
    o->commit = 0U;
    o->force = 0U;
    while ((i < argc) && (end_opts == 0) && (argv[i][0] == '-') &&
           (argv[i][1] != '\0')) {
        if (strcmp(argv[i], "--") == 0) {
            end_opts = 1;
        } else if (((allowed & CMD_OPT_COMMIT) != 0U) &&
                   ((strcmp(argv[i], "-c") == 0) ||
                    (strcmp(argv[i], "--commit") == 0))) {
            o->commit = 1U;
        } else if (((allowed & CMD_OPT_COMMIT) != 0U) &&
                   (strcmp(argv[i], "-m") == 0)) {
            if ((i + 1) >= argc) {
                msg_error(MSG_ERR_OPT_VALUE, argv[i]);
                goto exit;
            }
            i++;
            o->message = argv[i];
            o->commit = 1U;
        } else if (((allowed & CMD_OPT_FORCE) != 0U) &&
                   ((strcmp(argv[i], "-f") == 0) ||
                    (strcmp(argv[i], "--force") == 0))) {
            o->force = 1U;
        } else {
            msg_error(MSG_ERR_UNKNOWN_OPT, argv[i]);
            goto exit;
        }
        i++;
    }
    rc = i;

exit:
    if (rc < 0) {
        fputs(msg(usage), stderr);
    }
    return rc;
}

int cmd_open(Repo_t *r, GitBatch_t *b)
{
    int rc = GS_EXIT_FATAL;
    eGsErr_t err = repo_open(r);

    if (err != GS_OK) {
        msg_fatal((err == GS_ERR_NOT_REPO)  ? MSG_ERR_NOT_REPO
                  : (err == GS_ERR_FORMAT) ? MSG_ERR_READ_GITSHARED
                                           : MSG_ERR_PROC,
                  "git");
        repo_close(r);
    } else if (git_batch_open(b) != GS_OK) {
        msg_fatal(MSG_ERR_PROC, "git cat-file");
        repo_close(r);
    } else {
        rc = GS_EXIT_OK;
        if (repo_complete_pending(r, b) != GS_OK) {
            msg_error(MSG_ERR_PENDING); /* si prosegue: lo stato lo mostra */
        }
    }
    return rc;
}

/* "git config shared.<id>.<field> <value>" */
static eGsErr_t set_key(const char *id, const char *field, const char *value)
{
    const char *args[] = { "config", NULL, value, NULL };
    Str_t key = STR_INIT;
    eGsErr_t err;

    str_addf(&key, "shared.%s.%s", id, field);
    args[1] = str_cstr(&key);
    err = git_run(args, 0U, NULL);
    str_free(&key);
    return err;
}

eGsErr_t cmd_set_pending(const Repo_t *r, const char *id, const char *op,
                         const char *path, const char *src)
{
    /* "pending" per ultima: senza di lei le altre chiavi sono ignorate,
     * quindi un'interruzione non lascia un'attesa senza branch (D-077). */
    eGsErr_t err = set_key(id, "pendingPath", path);

    if ((err == GS_OK) && (r->branch.len > 0U)) {
        err = set_key(id, "pendingBranch", str_cstr(&r->branch));
    }
    if ((err == GS_OK) && (src != NULL)) {
        err = set_key(id, "pendingSource", src);
    }
    if (err == GS_OK) {
        err = set_key(id, "pending", op);
    }
    return err;
}

eGsErr_t cmd_write_gitshared(const Repo_t *r, const char *name,
                             const char *id, const char *path)
{
    const char *args[] = { "config", "-f", NULL, NULL, NULL, NULL, NULL };
    Str_t file = STR_INIT;
    Str_t key = STR_INIT;
    FILE *f;
    int created = 0;
    int put_rc;
    int close_rc;
    eGsErr_t err = GS_ERR_SYS;

    str_addf(&file, "%s/.gitshared", str_cstr(&r->top));
    f = sys_fopen(str_cstr(&file), "rb");
    if (f != NULL) {
        (void)fclose(f);
    } else {
        f = sys_fopen(str_cstr(&file), "wb");
        if (f == NULL) {
            goto exit;
        }
        created = 1;
        put_rc = fputs(gitshared_header, f);
        close_rc = fclose(f);
        if ((put_rc < 0) || (close_rc != 0)) {
            goto exit;
        }
    }

    args[2] = str_cstr(&file);
    str_addf(&key, "shared.%s.id", name);
    args[3] = str_cstr(&key);
    args[4] = id;
    err = git_run(args, 0U, NULL);
    if (err == GS_OK) {
        str_reset(&key);
        str_addf(&key, "shared.%s.path", name);
        args[3] = str_cstr(&key);
        args[4] = path;
        err = git_run(args, 0U, NULL);
    }
    if ((err == GS_OK) && (r->branch.len > 0U)) {
        str_reset(&key);
        str_addf(&key, "shared.%s.branch", name);
        args[3] = "--add";
        args[4] = str_cstr(&key);
        args[5] = str_cstr(&r->branch);
        err = git_run(args, 0U, NULL);
    }
    if ((err != GS_OK) && (created == 0)) {
        str_reset(&key);
        str_addf(&key, "shared.%s", name);
        args[3] = "--remove-section";
        args[4] = str_cstr(&key);
        args[5] = NULL;
        (void)git_run(args, PROC_STDERR_NULL, NULL);
    }

exit:
    if ((err != GS_OK) && (created != 0)) {
        (void)sys_remove(str_cstr(&file));
    }
    str_free(&key);
    str_free(&file);
    return err;
}

eGsErr_t cmd_stage(const Repo_t *r, const char *path)
{
    const char *args[] = { "-C", NULL, "update-index", "--add", "--", path,
                           NULL };

    args[1] = str_cstr(&r->top);
    return git_run(args, 0U, NULL);
}

eGsErr_t cmd_write_file(const Repo_t *r, const char *path, const Oid_t *blob)
{
    const char *a_index[] = { "-C", NULL, "update-index", "--add",
                              "--cacheinfo", NULL, NULL };
    const char *a_out[] = { "-C", NULL, "checkout-index", "-f", "--", path,
                            NULL };
    char hex[OID_HEX_SIZE];
    Str_t info = STR_INIT;
    eGsErr_t err;

    /* Modo sempre 100644 nella v0 (bit di esecuzione: A-15). */
    oid_to_hex(blob, hex);
    str_addf(&info, "100644,%s,%s", hex, path);
    a_index[1] = str_cstr(&r->top);
    a_index[5] = str_cstr(&info);
    err = git_run(a_index, 0U, NULL);
    if (err == GS_OK) {
        a_out[1] = str_cstr(&r->top);
        err = git_run(a_out, 0U, NULL);
    }
    str_free(&info);
    return err;
}

int cmd_worktree_differs(const Repo_t *r, const char *path)
{
    /* "git diff" senza revisioni confronta l'index con il working tree,
     * normalizzando i fine riga come fa git: 0 = uguali, 1 = diversi. */
    const char *args[] = { "-C", NULL, "--literal-pathspecs", "diff",
                           "--quiet", "--", path, NULL };
    Str_t out = STR_INIT;
    int code = 1;

    args[1] = str_cstr(&r->top);
    if (git_capture(args, PROC_STDERR_NULL, &out, &code) != GS_OK) {
        code = 1;
    }
    str_free(&out);
    return code != 0;
}

int cmd_has_local_changes(const Repo_t *r, const SharedEntry_t *e, GitBatch_t *b)
{
    const char *path = str_cstr(&e->path);
    OidList_t chain = OIDLIST_INIT;
    Str_t name = STR_INIT;
    Oid_t h;
    Oid_t i;
    int local = 1; /* nel dubbio, il lavoro dell'utente si protegge */

    /* 1. Working tree diverso dall'index: modifica non in staging. */
    if (cmd_worktree_differs(r, path)) {
        goto exit;
    }
    /* 2. Index (stage 0; assente se in conflitto o tolto dall'index). */
    str_addf(&name, ":%s", path);
    if (git_batch_blob(b, str_cstr(&name), &i) == 0) {
        goto exit;
    }
    /* 3. Index uguale a HEAD: nessuna modifica. */
    str_reset(&name);
    str_addf(&name, "HEAD:%s", path);
    if ((git_batch_blob(b, str_cstr(&name), &h) != 0) && oid_eq(&i, &h)) {
        local = 0;
        goto exit;
    }
    /* 4. Index uguale a una versione della catena: e' un update applicato
     * e non committato, non lavoro dell'utente (D-076). */
    if ((chain_blobs(b, e->id, &chain) == GS_OK) && oidlist_has(&chain, &i)) {
        local = 0;
    }

exit:
    oidlist_free(&chain);
    str_free(&name);
    return local;
}

eGsErr_t cmd_commit(const Repo_t *r, const char *message, const Str_t *paths)
{
    size_t n = str_zcount(paths);
    const char **args = xmalloc((n + 10U) * sizeof *args);
    const char *p;
    size_t i = 0U;
    eGsErr_t err;

    /* --only: solo questi path, anche se in staging c'e' altro (B-25).
     * --literal-pathspecs: un nome con '*' o '?' non diventa un glob che
     * fa entrare altri file nel commit (C-06). */
    args[i++] = "-C";
    args[i++] = str_cstr(&r->top);
    args[i++] = "--literal-pathspecs";
    args[i++] = "commit";
    args[i++] = "-q";
    args[i++] = "-m";
    args[i++] = message;
    args[i++] = "--only";
    args[i++] = "--";
    for (p = str_znext(paths, NULL); p != NULL; p = str_znext(paths, p)) {
        args[i++] = p;
    }
    args[i] = NULL;
    err = git_run(args, 0U, NULL);
    free((void *)args);
    return err;
}

int cmd_reopen(Repo_t *r, GitBatch_t *b)
{
    int rc = GS_EXIT_OK;

    git_batch_close(b);
    if (git_batch_open(b) != GS_OK) {
        /* un cat-file nuovo, che veda il nuovo HEAD */
        msg_fatal(MSG_ERR_PROC, "git cat-file");
        repo_close(r);
        rc = GS_EXIT_FATAL;
    } else if (repo_complete_pending(r, b) != GS_OK) {
        msg_error(MSG_ERR_PENDING);
    } else {
        /* completato */
    }
    return rc;
}

/* Nuova versione: blob di HEAD, genitore T, ref condizionata a T. */
static eGsErr_t new_version(const SharedEntry_t *e, GitBatch_t *b)
{
    Str_t name = STR_INIT;
    Str_t ref = STR_INIT;
    Oid_t h;
    Oid_t t;
    Oid_t v;
    eGsErr_t err = GS_ERR_GIT;

    str_addf(&name, "HEAD:%s", str_cstr(&e->path));
    chain_ref(&ref, e->id);
    if ((git_batch_blob(b, str_cstr(&name), &h) != 0) &&
        (git_batch_query(b, str_cstr(&ref), &t) == GIT_OBJ_COMMIT)) {
        err = chain_commit(&h, &t, str_cstr(&e->name), &v);
        if (err == GS_OK) {
            /* Condizionata: se nel frattempo un peer ha cambiato T, non
             * si sovrascrive niente (REQ-NF-005, D-072). */
            err = git_update_ref(str_cstr(&ref), &v, &t, "git shared: publish");
        }
    }
    str_free(&ref);
    str_free(&name);
    return err;
}

int cmd_publish_entry(const SharedEntry_t *e, GitBatch_t *b, int create)
{
    PeerTally_t tally = { 0U, 0U, 0U };
    const char *label = str_cstr(&e->path);
    unsigned npeers = (unsigned)str_zcount(&e->peers);
    int rc = GS_EXIT_OK;

    if (create != 0) {
        if (new_version(e, b) != GS_OK) {
            msg_error(MSG_ERR_PUBLISH, label);
            rc = GS_EXIT_ERROR;
            goto exit;
        }
        printf(msg(MSG_PUB_CREATED), label);
        putchar('\n');
    }
    if (npeers == 0U) {
        printf(msg(MSG_PUB_NO_PEERS), label);
        putchar('\n');
        goto exit;
    }
    peers_push(label, e->id, &e->peers, &tally);
    printf(msg(MSG_PUB_PUSHED), label, tally.ok, npeers);
    putchar('\n');
    if (tally.rejected > 0U) {
        rc = GS_EXIT_ERROR;
    }

exit:
    return rc;
}

int cmd_repo_path(const Repo_t *r, const char *user_path, Str_t *out)
{
    return !path_is_absolute(user_path) &&
           path_join_norm(str_cstr(&r->prefix), user_path, out);
}

int cmd_select(Repo_t *r, const char *user_path, unsigned char *sel)
{
    Str_t full = STR_INIT;
    const SharedEntry_t *e;
    size_t i;
    int ok = 1;

    if ((user_path != NULL) && !cmd_repo_path(r, user_path, &full)) {
        str_reset(&full); /* nessuna voce avra' questo path */
    }
    for (i = 0U; i < r->n; i++) {
        e = &r->v[i];
        sel[i] = (e->in_gitshared && e->valid && e->in_config &&
                  (e->pending == SHARED_PENDING_NONE) &&
                  ((user_path == NULL) || str_eq(&e->path, str_cstr(&full))))
                     ? 1U
                     : 0U;
    }
    if ((user_path != NULL) && (repo_find_path(r, str_cstr(&full)) == NULL)) {
        msg_error(MSG_ERR_NOT_SHARED_PATH, user_path);
        ok = 0;
    }
    str_free(&full);
    return ok;
}
