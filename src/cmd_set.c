/*
 * cmd_set.c - "git shared set [-c | -m <msg>] [--] <path>" (REQ-F-001).
 *
 * Al comando (REQ-F-023): operazione in attesa in .git/config, sezione in
 * .gitshared, .gitshared in staging. Al commit: catena e branch attivi
 * (repo_complete_pending). Con -c/-m il commit si fa subito.
 *
 * L'attesa e' scritta per prima: se un passo successivo fallisce resta
 * un'attesa senza sezione, che il primo comando git shared scarta.
 *
 * File gia' dichiarato in .gitshared ma senza dati locali ("scollegato",
 * es. un'attesa scartata durante un git stash e poi tornata): set non
 * rifiuta, riusa l'ID e il nome che ci sono (D-079).
 */
#include <stdio.h>
#include <string.h>

#include "cmd.h"
#include "cmd_common.h"
#include "hooks.h"
#include "sys.h"

int cmd_set(int argc, char **argv)
{
    CmdOpts_t o;
    Repo_t repo = REPO_INIT;
    GitBatch_t batch;
    SharedEntry_t *e;
    Str_t full = STR_INIT;
    Str_t text = STR_INIT;
    Str_t paths = STR_INIT;
    Str_t name_buf = STR_INIT;
    const char *name;
    const char *slash;
    char id[GS_ID_HEX_LEN + 1U];
    int first;
    int readopt = 0;
    int opened = 0;
    int hooks = 0;
    int rc = GS_EXIT_USAGE;

    first = cmd_parse(argc, argv, CMD_OPT_COMMIT, MSG_USAGE_SET, &o);
    if (first < 0) {
        goto exit;
    }
    if ((first + 1) != argc) {
        msg_error((first >= argc) ? MSG_ERR_MISSING_PATH : MSG_ERR_TOO_MANY_ARGS);
        fputs(msg(MSG_USAGE_SET), stderr);
        goto exit;
    }
    rc = cmd_open(&repo, &batch);
    if (rc != GS_EXIT_OK) {
        goto exit;
    }
    opened = 1;

    rc = GS_EXIT_ERROR;
    if (cmd_resolve_tracked(NULL, argv[first], &full) != GS_OK) {
        goto exit;
    }
    if (shared_path_reserved(str_cstr(&full))) {
        msg_error(MSG_ERR_PATH_RESERVED, str_cstr(&full)); /* B-36 */
        goto exit;
    }
    e = repo_find_path(&repo, str_cstr(&full));
    if ((e != NULL) && e->valid && !e->in_config) {
        /* dichiarato ma scollegato: si riprende con il suo ID (D-079) */
        readopt = 1;
        str_add(&name_buf, str_cstr(&e->name));
        name = str_cstr(&name_buf);
        memcpy(id, e->id, sizeof id);
    } else if (e != NULL) {
        msg_error(MSG_ERR_ALREADY_SHARED, str_cstr(&full), str_cstr(&e->name));
        goto exit;
    } else {
        slash = strrchr(str_cstr(&full), '/');
        name = (slash != NULL) ? (slash + 1) : str_cstr(&full);
        if (repo_find_name(&repo, name) != NULL) {
            msg_error(MSG_ERR_NAME_USED, name);
            goto exit;
        }
        if (shared_new_id(id) != GS_OK) {
            msg_fatal(MSG_ERR_RANDOM);
            goto exit;
        }
    }

    /* 1. Attesa (locale): per prima, vedi intestazione. */
    if (cmd_set_pending(&repo, id, "set", str_cstr(&full), NULL, 0, NULL) != GS_OK) {
        msg_error(MSG_ERR_PENDING);
        goto exit;
    }
    /* 2. Sezione in .gitshared; 3. staging (plumbing, REQ-NF-006). */
    /* In ripresa .gitshared c'e' gia': dopo uno stash pop puo' non essere
     * in staging, e cmd_stage qui sotto lo rimette. */
    if ((readopt == 0) &&
        (cmd_write_gitshared(&repo, name, id, str_cstr(&full)) != GS_OK)) {
        msg_error(MSG_ERR_WRITE_GITSHARED);
        goto exit;
    }
    if (cmd_stage(&repo, ".gitshared") != GS_OK) {
        msg_error(MSG_ERR_STAGE_GITSHARED);
        goto exit;
    }
    hooks = 1; /* REQ-F-016: installati alla fine, vedi exit */

    if (readopt == 0) {
        e = repo_add(&repo);
        str_add(&e->name, name);
        str_add(&e->path, str_cstr(&full));
    } else {
        e = repo_find_path(&repo, str_cstr(&full));
    }
    str_add(&e->pending_path, str_cstr(&full));
    str_add(&e->pending_branch, str_cstr(&repo.branch));
    memcpy(e->id, id, sizeof id);
    e->in_gitshared = 1U;
    e->in_config = 1U;
    e->valid = 1U;
    e->pending = SHARED_PENDING_SET;

    /* Ripresa: se la sezione e' gia' committata non c'e' niente da
     * committare, e la dichiarazione vale subito. */
    if (readopt != 0) {
        if (cmd_reopen(&repo, &batch) != GS_EXIT_OK) {
            opened = 0;
            goto exit;
        }
        e = repo_find_path(&repo, str_cstr(&full));
        if ((e != NULL) && (e->pending == SHARED_PENDING_NONE)) {
            printf(msg(MSG_SET_DONE), str_cstr(&full), name);
            putchar('\n');
            rc = GS_EXIT_OK;
            goto exit;
        }
    }

    if (o.commit == 0U) {
        printf(msg(MSG_SET_STAGED), str_cstr(&full), name);
        putchar('\n');
        rc = GS_EXIT_OK;
        goto exit;
    }

    /* 4. Commit dedicato del solo .gitshared (D-007, A-29). */
    if (o.message == NULL) {
        str_addf(&text, msg(MSG_SET_COMMIT_MSG), str_cstr(&full));
        o.message = str_cstr(&text);
    }
    str_addz(&paths, ".gitshared");
    if (cmd_commit(&repo, o.message, &paths) != GS_OK) {
        msg_error(MSG_ERR_COMMIT);
        goto exit;
    }
    /* Completamento con un cat-file che veda il nuovo HEAD. */
    if (cmd_reopen(&repo, &batch) != GS_EXIT_OK) {
        opened = 0;
        goto exit;
    }
    /* Se il file non e' ancora in HEAD resta in attesa: l'avviso e' gia'
     * stato dato da repo_complete_pending. */
    e = repo_find_path(&repo, str_cstr(&full));
    if ((e != NULL) && (e->pending == SHARED_PENDING_NONE)) {
        printf(msg(MSG_SET_DONE), str_cstr(&full), name);
        putchar('\n');
    }
    rc = GS_EXIT_OK;

exit:
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
    str_free(&name_buf);
    str_free(&paths);
    str_free(&text);
    str_free(&full);
    return rc;
}
