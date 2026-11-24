/*
 * cmd_publish.c - "git shared publish [<path>]"
 * (REQ-F-004, REQ-F-005, REQ-F-006, REQ-F-008).
 *
 * Per ogni file shared (o quello indicato), prima si riceve (D-099), poi:
 *   da pubblicare -> nuova versione con genitore T (la base e' T, D-065),
 *                    poi avviso ai peer;
 *   allineato     -> avviso di T ai soli peer che non hanno T ne' il suo
 *                    avviso (es. peer prima irraggiungibili); se ce l'hanno
 *                    tutti, "gia' pubblicata" e niente altro;
 *   altri stati   -> niente: si dice cosa fare.
 * Una versione ricevuta che non discende da T va prima unita (update).
 * Ai peer con .git file si fa il push, che non forza mai (D-040).
 *
 * Sempre locale (D-110): un file con creatore va al creatore con "git
 * shared push" (cmd_push.c); publish lo ricorda se il creatore, all'ultima
 * volta che lo si e' visto, non aveva T.
 */
#include <stdio.h>
#include <stdlib.h>

#include "cmd.h"
#include "cmd_common.h"
#include "mem.h"
#include "receive.h"
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
    StateInfo_t si;
    BranchNames_t names = BRANCHNAMES_INIT;
    Received_t rv = RECEIVED_INIT;
    Oid_t tip;
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
        /* Prima si riceve (D-099): una versione nuova di un peer o
         * l'ultima vista del creatore cambia lo stato. Una catena assente
         * la segnala lo stato. */
        (void)receive_entry(&repo, e, &batch, 0, &rv);
        if (state_derive(&repo, e, &batch, &st, &si) != GS_OK) {
            msg_fatal(MSG_ERR_PROC, "git");
            rc = GS_EXIT_FATAL;
            goto exit;
        }
        if (state_is_derived(st) && !shared_is_active(e, str_cstr(&repo.branch))) {
            cmd_warn_suspended(e, &names); /* atto esplicito: si fa */
        }
        if (((st == STATE_TO_PUBLISH) || (st == STATE_ALIGNED)) &&
            (rv.heads.n > 0U)) {
            /* La versione di un peer non discende da T: prima si unisce,
             * come quando il push veniva rifiutato (REQ-F-011). */
            msg_error(MSG_PUB_TO_MERGE, label);
            rc = GS_EXIT_ERROR;
        } else if ((st == STATE_TO_PUBLISH) || (st == STATE_ALIGNED)) {
            tip = si.tip;
            if (cmd_publish_entry(&repo, e, &batch,
                                  (st == STATE_TO_PUBLISH) ? &si : NULL,
                                  &tip) != GS_EXIT_OK) {
                rc = GS_EXIT_ERROR;
            } else if ((rv.authority.len > 0U) && !oid_eq(&rv.auth_tip, &tip)) {
                /* In remoto la versione non va da sola (D-110). */
                printf(msg(MSG_PUB_NOT_ON_CREATOR), label, auth_role(rv.sovereign));
                putchar('\n');
            } else {
                /* pubblicato */
            }
        } else if (st == STATE_TO_APPLY) {
            msg_error(MSG_PUB_TO_APPLY, label);
            rc = GS_EXIT_ERROR;
        } else if (st == STATE_TO_COMMIT) {
            msg_error(MSG_UPD_TO_COMMIT, label);
            rc = GS_EXIT_ERROR;
        } else if (st == STATE_MERGING) {
            msg_error(MSG_PUB_MERGING, label);
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
    branchnames_free(&names);
    received_free(&rv);
    return rc;
}
