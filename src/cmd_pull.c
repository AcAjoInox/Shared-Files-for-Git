/*
 * cmd_pull.c - "git shared pull [-f] [--all] [-c | -m <msg>] [<path>]"
 * (REQ-F-028, D-110).
 *
 * Come "git pull" = fetch + merge: per i file scelti con un creatore, la
 * catena del creatore in refs/shared-creator/<id> (un fetch per creatore,
 * con tutte le catene), poi update con le stesse opzioni (cmd_update_run).
 * Un file il cui creatore non risponde, o non ha piu' il file, non si
 * aggiorna: la rete l'ha chiesta l'utente, quindi e' un errore. --abort,
 * --ours e --theirs non hanno bisogno di rete: si usano con update.
 */
#include <stdio.h>
#include <stdlib.h>

#include "cmd.h"
#include "cmd_common.h"
#include "auth.h"
#include "mem.h"
#include "remote.h"

int cmd_pull(int argc, char **argv)
{
    CmdOpts_t o;
    Repo_t repo = REPO_INIT;
    GitBatch_t batch;
    unsigned char *sel = NULL;
    unsigned char *res = NULL;
    unsigned char *sov = NULL;
    Str_t *urls = NULL;
    const char *path;
    const char *label;
    size_t n = 0U;
    size_t i;
    int first;
    int opened = 0;
    int batch_ok = 0;
    int rc = GS_EXIT_USAGE;

    first = cmd_parse(argc, argv, CMD_OPT_COMMIT | CMD_OPT_FORCE | CMD_OPT_ALL,
                      MSG_USAGE_PULL, &o);
    if (first < 0) {
        goto exit;
    }
    if ((argc - first) > 1) {
        msg_error(MSG_ERR_TOO_MANY_ARGS);
        fputs(msg(MSG_USAGE_PULL), stderr);
        goto exit;
    }
    path = (first < argc) ? argv[first] : NULL;
    rc = cmd_open(&repo, &batch);
    if (rc != GS_EXIT_OK) {
        goto exit;
    }
    opened = 1;
    batch_ok = 1;
    n = repo.n;
    sel = xmalloc((n > 0U) ? n : 1U);
    res = xmalloc((n > 0U) ? n : 1U);
    sov = xmalloc((n > 0U) ? n : 1U);
    urls = xmalloc(((n > 0U) ? n : 1U) * sizeof *urls);
    for (i = 0U; i < n; i++) {
        str_init(&urls[i]);
    }
    if (!cmd_select(&repo, path, sel)) {
        rc = GS_EXIT_ERROR;
        goto exit;
    }

    /* 1. fetch dalle autorita' (D-111: l'ultimo sovrano, o il creatore) */
    cmd_fetch_creators(&repo, &batch, sel, res, urls, sov);
    for (i = 0U; i < n; i++) {
        if (sel[i] == 0U) {
            continue;
        }
        label = str_cstr(&repo.v[i].path);
        if (res[i] == CMD_NO_CREATOR) {
            if (path != NULL) {
                msg_error(MSG_ERR_PULL_NO_CREATOR, label, label);
                rc = GS_EXIT_ERROR;
                sel[i] = 0U;
            } /* senza path: update come per ogni file locale */
        } else if (res[i] == (unsigned char)REMOTE_MISSING) {
            msg_error(MSG_ERR_CREATOR_MISSING, label, auth_role(sov[i]),
                      str_cstr(&urls[i]));
            rc = GS_EXIT_ERROR;
            sel[i] = 0U;
        } else if (res[i] == (unsigned char)REMOTE_UNREACHABLE) {
            msg_error(MSG_ERR_CREATOR_UNREACHABLE, label, auth_role(sov[i]),
                      str_cstr(&urls[i]));
            rc = GS_EXIT_ERROR;
            sel[i] = 0U;
        } else {
            /* ultima versione vista aggiornata */
        }
    }

    /* 2. update, con un cat-file che veda le ref e gli oggetti nuovi */
    git_batch_close(&batch);
    if (git_batch_open(&batch) != GS_OK) {
        msg_fatal(MSG_ERR_PROC, "git cat-file");
        batch_ok = 0;
        rc = GS_EXIT_FATAL;
        goto exit;
    }
    rc = cmd_update_run(&repo, &batch, sel, &o, rc, &batch_ok);

exit:
    if (urls != NULL) {
        for (i = 0U; i < n; i++) {
            str_free(&urls[i]);
        }
    }
    free(urls);
    free(sov);
    free(res);
    free(sel);
    if (opened != 0) {
        if (batch_ok != 0) {
            git_batch_close(&batch);
        }
        repo_close(&repo);
    }
    return rc;
}
