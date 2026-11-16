/*
 * cmd_publish.c - "git shared publish [<path>]" (REQ-F-004, REQ-F-005,
 * REQ-F-006, REQ-F-008).
 *
 * Per ogni file shared (o quello indicato):
 *   da pubblicare -> nuova versione con genitore T (la base e' T, D-065),
 *                    poi push ai peer;
 *   allineato     -> push di T ai peer (es. peer prima irraggiungibili);
 *   altri stati   -> niente: si dice cosa fare.
 * Il push non forza mai: un peer con una versione nuova lo rifiuta (D-040).
 */
#include <stdio.h>
#include <stdlib.h>

#include "cmd.h"
#include "cmd_common.h"
#include "mem.h"
#include "state.h"

int cmd_publish(int argc, char **argv)
{
    CmdOpts_t o;
    Repo_t repo = REPO_INIT;
    GitBatch_t batch;
    const SharedEntry_t *e;
    const char *label;
    unsigned char *sel = NULL;
    eState_t st;
    size_t i;
    int first;
    int opened = 0;
    int rc = GS_EXIT_USAGE;

    first = cmd_parse(argc, argv, 0U, MSG_USAGE_PUBLISH, &o);
    if (first < 0) {
        goto exit;
    }
    if ((argc - first) > 1) {
        msg_error(MSG_ERR_TOO_MANY_ARGS);
        fputs(msg(MSG_USAGE_PUBLISH), stderr);
        goto exit;
    }
    rc = cmd_open(&repo, &batch);
    if (rc != GS_EXIT_OK) {
        goto exit;
    }
    opened = 1;
    sel = xmalloc((repo.n > 0U) ? repo.n : 1U);
    if (!cmd_select(&repo, (first < argc) ? argv[first] : NULL, sel)) {
        rc = GS_EXIT_ERROR;
        goto exit;
    }

    for (i = 0U; i < repo.n; i++) {
        e = &repo.v[i];
        if (sel[i] == 0U) {
            continue;
        }
        label = str_cstr(&e->path);
        if (state_derive(&repo, e, &batch, &st) != GS_OK) {
            msg_fatal(MSG_ERR_PROC, "git");
            rc = GS_EXIT_FATAL;
            goto exit;
        }
        if (state_is_derived(st) && !shared_is_active(e, str_cstr(&repo.branch))) {
            msg_warning(MSG_WARN_SUSPENDED, label); /* atto esplicito: si fa */
        }
        if ((st == STATE_TO_PUBLISH) || (st == STATE_ALIGNED)) {
            if (cmd_publish_entry(e, &batch, st == STATE_TO_PUBLISH) != GS_EXIT_OK) {
                rc = GS_EXIT_ERROR;
            }
        } else if (st == STATE_TO_APPLY) {
            msg_error(MSG_PUB_TO_APPLY, label);
            rc = GS_EXIT_ERROR;
        } else if (st == STATE_TO_COMMIT) {
            msg_error(MSG_UPD_TO_COMMIT, label);
            rc = GS_EXIT_ERROR;
        } else if (st == STATE_DIVERGED) {
            msg_error(MSG_PUB_DIVERGED, label);
            rc = GS_EXIT_ERROR;
        } else {
            printf(msg(MSG_SKIP_STATE), label, state_name(st));
            putchar('\n');
        }
    }

exit:
    free(sel);
    if (opened != 0) {
        git_batch_close(&batch);
        repo_close(&repo);
    }
    return rc;
}
