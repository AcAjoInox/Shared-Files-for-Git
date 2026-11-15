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

#include "chain.h"
#include "cmd.h"
#include "cmd_common.h"
#include "mem.h"
#include "peers.h"
#include "state.h"

/* Crea la nuova versione: blob di HEAD, genitore T, ref condizionata. */
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
            err = git_update_ref(str_cstr(&ref), &v, &t, "git shared: publish");
        }
    }
    str_free(&ref);
    str_free(&name);
    return err;
}

int cmd_publish(int argc, char **argv)
{
    CmdOpts_t o;
    Repo_t repo = REPO_INIT;
    GitBatch_t batch;
    PeerTally_t tally;
    const SharedEntry_t *e;
    const char *label;
    unsigned char *sel = NULL;
    unsigned npeers;
    eState_t st;
    size_t i;
    int first;
    int push;
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
        push = 0;
        if (st == STATE_TO_PUBLISH) {
            if (new_version(e, &batch) != GS_OK) {
                msg_error(MSG_ERR_PUBLISH, label);
                rc = GS_EXIT_ERROR;
            } else {
                printf(msg(MSG_PUB_CREATED), label);
                putchar('\n');
                push = 1;
            }
        } else if (st == STATE_ALIGNED) {
            push = 1;
        } else if (st == STATE_TO_APPLY) {
            msg_error(MSG_PUB_TO_APPLY, label);
            rc = GS_EXIT_ERROR;
        } else if (st == STATE_DIVERGED) {
            msg_error(MSG_PUB_DIVERGED, label);
            rc = GS_EXIT_ERROR;
        } else {
            printf(msg(MSG_SKIP_STATE), label, state_name(st));
            putchar('\n');
        }

        npeers = (unsigned)str_zcount(&e->peers);
        if ((push != 0) && (npeers == 0U)) {
            printf(msg(MSG_PUB_NO_PEERS), label);
            putchar('\n');
        } else if (push != 0) {
            tally.ok = 0U;
            tally.rejected = 0U;
            tally.unreachable = 0U;
            peers_push(label, e->id, &e->peers, &tally);
            printf(msg(MSG_PUB_PUSHED), label, tally.ok, npeers);
            putchar('\n');
            if (tally.rejected > 0U) {
                rc = GS_EXIT_ERROR;
            }
        } else {
            /* niente da inviare */
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
