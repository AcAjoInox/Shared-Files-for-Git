/*
 * cmd_status.c - "git shared status" (REQ-F-012).
 *
 * Processi git nel caso comune (file allineato), su qualunque numero di
 * file: tre in parallelo (radice, branch, config locale), poi .gitshared
 * e un solo cat-file per tutte le domande. La storia di un file si legge
 * solo quando H e T differiscono.
 */
#include <stdio.h>

#include "cmd.h"
#include "err.h"
#include "git.h"
#include "msg.h"
#include "shared.h"
#include "state.h"

static void print_entry(const Repo_t *r, const SharedEntry_t *e, eState_t st,
                        int suspended)
{
    if (e->in_gitshared) {
        printf("%s\n", str_cstr(&e->name));
        printf("    %-6s %s\n", msg(MSG_STATUS_FIELD_PATH), str_cstr(&e->path));
    } else if ((st == STATE_PENDING) && (e->pending_path.len > 0U)) {
        /* attesa nata su un altro branch: qui .gitshared non la dichiara */
        printf("%s\n", str_cstr(&e->pending_path));
        printf("    %-6s %s\n", msg(MSG_STATUS_FIELD_PATH),
               str_cstr(&e->pending_path));
    } else {
        printf("%s\n", msg(MSG_STATUS_UNDECLARED));
    }
    printf("    %-6s %s\n", msg(MSG_STATUS_FIELD_ID), e->id);
    printf("    %-6s ", msg(MSG_STATUS_FIELD_STATE));
    if (suspended != 0) {
        printf(msg(MSG_STATUS_SUSPENDED), state_name(st));
    } else if ((st == STATE_PENDING) && (e->pending_branch.len > 0U) &&
               !str_eq(&r->branch, str_cstr(&e->pending_branch))) {
        /* il commit di conferma si aspetta su quel branch (D-077) */
        printf(msg(MSG_STATUS_PENDING_ON), state_name(st),
               str_cstr(&e->pending_branch));
    } else {
        fputs(state_name(st), stdout);
    }
    putchar('\n');
}

int cmd_status(int argc, char **argv)
{
    Repo_t repo = REPO_INIT;
    GitBatch_t batch;
    const SharedEntry_t *e;
    eState_t st;
    size_t i;
    size_t shown = 0U;
    int batch_open = 0;
    int suspended;
    int rc = GS_EXIT_FATAL;
    eGsErr_t err;

    (void)argv;
    if (argc > 1) {
        msg_error(MSG_ERR_TOO_MANY_ARGS);
        fputs(msg(MSG_USAGE_STATUS), stderr);
        rc = GS_EXIT_USAGE;
        goto exit;
    }
    err = repo_open(&repo);
    if (err != GS_OK) {
        msg_fatal((err == GS_ERR_NOT_REPO)  ? MSG_ERR_NOT_REPO
                  : (err == GS_ERR_FORMAT) ? MSG_ERR_READ_GITSHARED
                                           : MSG_ERR_PROC,
                  "git");
        goto exit;
    }
    if (git_batch_open(&batch) != GS_OK) {
        msg_fatal(MSG_ERR_PROC, "git cat-file");
        goto exit;
    }
    batch_open = 1;
    rc = GS_EXIT_OK;
    if (repo_complete_pending(&repo, &batch) != GS_OK) {
        msg_error(MSG_ERR_PENDING);
        rc = GS_EXIT_ERROR; /* si mostra comunque lo stato */
    }

    for (i = 0U; i < repo.n; i++) {
        e = &repo.v[i];
        if (e->in_gitshared && !e->valid) {
            msg_warning(MSG_WARN_BAD_ENTRY, str_cstr(&e->name));
        } else if (e->in_gitshared || e->in_config) {
            if (state_derive(&repo, e, &batch, &st) != GS_OK) {
                msg_fatal(MSG_ERR_PROC, "git");
                rc = GS_EXIT_FATAL;
                goto exit;
            }
            suspended = state_is_derived(st) &&
                        !shared_is_active(e, str_cstr(&repo.branch));
            if (shown > 0U) {
                putchar('\n');
            }
            print_entry(&repo, e, st, suspended);
            shown++;
        } else {
            /* attesa appena scartata: niente da mostrare */
        }
    }
    if (shown == 0U) {
        puts(msg(MSG_STATUS_NONE));
    }

exit:
    if (batch_open != 0) {
        git_batch_close(&batch);
    }
    repo_close(&repo);
    return rc;
}
