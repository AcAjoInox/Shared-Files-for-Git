/*
 * cmd_mv.c - "git shared mv [-c | -m <msg>] [--] <path> <dest>"
 * (REQ-F-020, D-045, D-101).
 *
 * Rinomina un file shared: git mv, poi il path nuovo in .gitshared, con il
 * path di prima tra gli "oldpath" della sezione: la storia del file
 * continua da li' (state_find_base). La rinomina e' solo locale: i peer non
 * ne sono toccati e il nome nella catena e' fisso (D-042). Nessuna attesa
 * in .git/config: il path e' scritto solo in .gitshared (D-068). Finche'
 * la rinomina non e' committata il file e' "in attesa di commit"
 * (state_renamed); git restore la annulla come ogni modifica in staging.
 *
 * Come git mv: se <dest> e' una cartella il file va dentro, con il suo
 * nome. Il file gia' rinominato con git mv (stato incoerente: il path di
 * .gitshared non c'e' piu') si sistema con lo stesso comando, che allora
 * aggiorna solo .gitshared.
 */
#include <stdio.h>
#include <string.h>

#include "cmd.h"
#include "cmd_common.h"
#include "merge.h"
#include "sys.h"

/* "git config -f .gitshared <a1> <a2> [<a3> [<a4>]]"; code puo' essere
 * NULL (allora conta solo 0). */
static eGsErr_t gitshared_config(const Repo_t *r, const char *a1,
                                 const char *a2, const char *a3,
                                 const char *a4, int *code)
{
    const char *args[] = { "config", "-f", NULL, a1, a2, a3, a4, NULL };
    Str_t file = STR_INIT;
    Str_t out = STR_INIT;
    eGsErr_t err;

    str_addf(&file, "%s/.gitshared", str_cstr(&r->top));
    args[2] = str_cstr(&file);
    if (code == NULL) {
        err = git_run(args, 0U, NULL);
    } else {
        err = git_capture(args, PROC_STDERR_NULL, &out, code);
    }
    str_free(&out);
    str_free(&file);
    return err;
}

/*
 * .gitshared: path nuovo; il vecchio tra gli oldpath (una volta sola);
 * il nuovo tolto dagli oldpath, se ci si torna (mv b a dopo mv a b).
 */
static eGsErr_t write_paths(const Repo_t *r, const SharedEntry_t *e,
                            const char *from, const char *to)
{
    Str_t key = STR_INIT;
    int code = 0;
    eGsErr_t err;

    str_addf(&key, "shared.%s.path", str_cstr(&e->name));
    err = gitshared_config(r, str_cstr(&key), to, NULL, NULL, NULL);
    str_reset(&key);
    str_addf(&key, "shared.%s.oldpath", str_cstr(&e->name));
    if ((err == GS_OK) && str_zhas(&e->old_paths, to)) {
        /* --fixed-value: il path e' un valore, non un'espressione regolare
         * (git >= 2.30). Codice 5 = non c'era. */
        err = gitshared_config(r, "--fixed-value", "--unset-all",
                               str_cstr(&key), to, &code);
        if ((err == GS_OK) && (code != 0) && (code != 5)) {
            err = GS_ERR_GIT;
        }
    }
    if ((err == GS_OK) && !str_zhas(&e->old_paths, from)) {
        err = gitshared_config(r, "--add", str_cstr(&key), from, NULL, NULL);
    }
    str_free(&key);
    return err;
}

/* 1 se il file ha un'unione in corso (D-078): non si sposta. */
static int merging(const SharedEntry_t *e, GitBatch_t *b)
{
    Str_t name = STR_INIT;
    Oid_t oid;
    Oid_t h;
    int unmerged = 0;
    int live = 0;

    merge_ref(&name, e->id);
    if (git_batch_query(b, str_cstr(&name), &oid) == GIT_OBJ_COMMIT) {
        str_reset(&name);
        str_addf(&name, "HEAD:%s", str_cstr(&e->path));
        live = git_batch_blob(b, str_cstr(&name), &h) &&
               merge_is_live(b, str_cstr(&e->path), &h, &unmerged);
    }
    str_free(&name);
    return live;
}

/*
 * Path di arrivo relativo alla radice: <dest>, o <dest>/<nome> se <dest> e'
 * una cartella (anche la radice). 0 se il path non va (errore stampato).
 */
static int dest_path(const Repo_t *r, const char *user_dest, const char *from,
                     Str_t *to)
{
    Str_t full = STR_INIT;
    Str_t dir = STR_INIT;
    const char *slash = strrchr(from, '/');
    const char *base = (slash != NULL) ? (slash + 1) : from;
    size_t n = strlen(user_dest);
    int ok = 0;

    if ((strchr(user_dest, '\n') == NULL) && cmd_repo_path(r, user_dest, to)) {
        str_addf(&full, "%s/%s", str_cstr(&r->top), str_cstr(to));
        if ((to->len == 0U) || sys_is_dir(str_cstr(&full))) {
            if (to->len > 0U) {
                str_addch(to, '/');
            }
            str_add(to, base);
            ok = 1;
        } else {
            /* "dir/" che non esiste: git mv rifiuta, e cosi' qui */
            ok = (n > 0U) && (user_dest[n - 1U] != '/') &&
                 (user_dest[n - 1U] != '\\');
        }
    }
    if (ok && str_eq(to, ".gitshared")) {
        ok = 0;
    }
    if (!ok) {
        msg_error(MSG_ERR_DEST_PATH, user_dest);
        goto exit;
    }
    /* La cartella di arrivo deve esistere, come per git mv: lo si dice
     * prima, con il suo nome. */
    slash = strrchr(str_cstr(to), '/');
    if (slash != NULL) {
        str_addn(&dir, str_cstr(to), (size_t)(slash - str_cstr(to)));
        str_reset(&full);
        str_addf(&full, "%s/%s", str_cstr(&r->top), str_cstr(&dir));
        if (!sys_is_dir(str_cstr(&full))) {
            msg_error(MSG_ERR_MV_NO_DIR, str_cstr(&dir));
            ok = 0;
        }
    }

exit:
    str_free(&dir);
    str_free(&full);
    return ok;
}

int cmd_mv(int argc, char **argv)
{
    const char *a_mv[] = { "-C", NULL, "mv", "--", NULL, NULL, NULL };
    CmdOpts_t o;
    Repo_t repo = REPO_INIT;
    GitBatch_t batch;
    SharedEntry_t *e = NULL;
    const SharedEntry_t *other;
    Str_t from = STR_INIT;
    Str_t to = STR_INIT;
    Str_t text = STR_INIT;
    Str_t paths = STR_INIT;
    Oid_t oid;
    int first;
    int opened = 0;
    int rc = GS_EXIT_USAGE;

    first = cmd_parse(argc, argv, CMD_OPT_COMMIT, MSG_USAGE_MV, &o);
    if (first < 0) {
        goto exit;
    }
    if ((argc - first) != 2) {
        msg_error((first >= argc)          ? MSG_ERR_MISSING_PATH
                  : ((argc - first) == 1) ? MSG_ERR_MISSING_DEST
                                          : MSG_ERR_TOO_MANY_ARGS);
        fputs(msg(MSG_USAGE_MV), stderr);
        goto exit;
    }
    rc = cmd_open(&repo, &batch);
    if (rc != GS_EXIT_OK) {
        goto exit;
    }
    opened = 1;
    rc = GS_EXIT_ERROR;

    /* --- il file shared ------------------------------------------------ */
    if (cmd_repo_path(&repo, argv[first], &from)) {
        e = repo_find_path(&repo, str_cstr(&from));
    }
    if ((e == NULL) || !e->valid) {
        msg_error(MSG_ERR_NOT_SHARED_PATH, argv[first]);
        goto exit;
    }
    if (e->pending != SHARED_PENDING_NONE) {
        msg_error(MSG_ERR_PENDING_OP, str_cstr(&from));
        goto exit;
    }
    if (merging(e, &batch)) {
        msg_error(MSG_ERR_MV_MERGING, str_cstr(&from), str_cstr(&from));
        goto exit;
    }

    /* --- dove --------------------------------------------------------- */
    if (!dest_path(&repo, argv[first + 1], str_cstr(&from), &to)) {
        goto exit;
    }
    if (str_eq(&to, str_cstr(&from))) {
        msg_error(MSG_ERR_MV_SAME, str_cstr(&from));
        goto exit;
    }
    other = repo_find_path(&repo, str_cstr(&to));
    if (other != NULL) {
        msg_error(MSG_ERR_ALREADY_SHARED, str_cstr(&to), str_cstr(&other->name));
        goto exit;
    }

    /* --- il file: git mv, se non e' gia' stato spostato ----------------- */
    str_addf(&text, ":%s", str_cstr(&from));
    if (git_batch_blob(&batch, str_cstr(&text), &oid)) {
        if (cmd_path_exists(&repo, &batch, str_cstr(&to))) {
            msg_error(MSG_ERR_DEST_EXISTS, str_cstr(&to));
            goto exit;
        }
        a_mv[1] = str_cstr(&repo.top);
        a_mv[4] = str_cstr(&from);
        a_mv[5] = str_cstr(&to);
        if (git_run(a_mv, 0U, NULL) != GS_OK) {
            msg_error(MSG_ERR_PROC, "git mv"); /* il motivo l'ha detto git */
            goto exit;
        }
    } else {
        /* gia' rinominato con git mv: deve essere nell'index al path nuovo */
        str_reset(&text);
        str_addf(&text, ":%s", str_cstr(&to));
        if (!git_batch_blob(&batch, str_cstr(&text), &oid)) {
            msg_error(MSG_ERR_MV_MISSING, str_cstr(&from), str_cstr(&to));
            goto exit;
        }
    }

    /* --- .gitshared ---------------------------------------------------- */
    if (write_paths(&repo, e, str_cstr(&from), str_cstr(&to)) != GS_OK) {
        msg_error(MSG_ERR_WRITE_GITSHARED);
        goto exit;
    }
    if (cmd_stage(&repo, ".gitshared") != GS_OK) {
        msg_error(MSG_ERR_STAGE_GITSHARED);
        goto exit;
    }

    if (o.commit == 0U) {
        printf(msg(MSG_MV_STAGED), str_cstr(&from), str_cstr(&to));
        putchar('\n');
        rc = GS_EXIT_OK;
        goto exit;
    }

    /* --- commit dedicato: .gitshared, il path vecchio e il nuovo -------- */
    str_addz(&paths, ".gitshared");
    str_addz(&paths, str_cstr(&to));
    /* il path vecchio solo se HEAD lo ha: --only rifiuta un path che git
     * non conosce (gia' spostato e committato con git mv) */
    str_reset(&text);
    str_addf(&text, "HEAD:%s", str_cstr(&from));
    if (git_batch_blob(&batch, str_cstr(&text), &oid)) {
        str_addz(&paths, str_cstr(&from));
    }
    str_reset(&text);
    if (o.message == NULL) {
        str_addf(&text, msg(MSG_MV_COMMIT_MSG), str_cstr(&from), str_cstr(&to));
        o.message = str_cstr(&text);
    }
    if (cmd_commit(&repo, o.message, &paths) != GS_OK) {
        msg_error(MSG_ERR_COMMIT);
        goto exit;
    }
    printf(msg(MSG_MV_DONE), str_cstr(&from), str_cstr(&to));
    putchar('\n');
    rc = GS_EXIT_OK;

exit:
    if (opened != 0) {
        git_batch_close(&batch);
        repo_close(&repo);
    }
    str_free(&paths);
    str_free(&text);
    str_free(&to);
    str_free(&from);
    return rc;
}
