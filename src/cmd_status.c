/*
 * cmd_status.c - "git shared status" (REQ-F-012).
 *
 * Processi git nel caso comune (file allineato), su qualunque numero di
 * file: tre in parallelo (radice, branch, config locale), poi .gitshared
 * e un solo cat-file per tutte le domande. La storia di un file si legge
 * solo quando H e T differiscono.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmd.h"
#include "chain.h"
#include "err.h"
#include "git.h"
#include "mem.h"
#include "merge.h"
#include "msg.h"
#include "peers.h"
#include "shared.h"
#include "state.h"

/* alt_path: path del file sul branch che lo dichiara, per un file non
 * dichiarato su questo (STATE_ABSENT); NULL altrimenti. */
static void print_entry(const Repo_t *r, const SharedEntry_t *e, eState_t st,
                        int suspended, const char *alt_path)
{
    if (e->in_gitshared) {
        printf("%s\n", str_cstr(&e->name));
        printf("    %-6s %s\n", msg(MSG_STATUS_FIELD_PATH), str_cstr(&e->path));
    } else if ((st == STATE_PENDING) && (e->pending_path.len > 0U)) {
        /* attesa nata su un altro branch: qui .gitshared non la dichiara */
        printf("%s\n", str_cstr(&e->pending_path));
        printf("    %-6s %s\n", msg(MSG_STATUS_FIELD_PATH),
               str_cstr(&e->pending_path));
    } else if (alt_path != NULL) {
        printf("%s\n", alt_path);
        printf("    %-6s %s\n", msg(MSG_STATUS_FIELD_PATH), alt_path);
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

/*
 * --peers (REQ-F-012, D-086): per ogni peer, se c'e', se condivide ancora
 * il file e com'e' la sua versione rispetto a T. Nessun fetch: si legge la
 * sua refs/shared/<id> e si confronta con la catena locale.
 */
static void print_peers(const SharedEntry_t *e, GitBatch_t *b)
{
    size_t n = str_zcount(&e->peers);
    uint8_t *state = xmalloc((n > 0U) ? n : 1U);
    Oid_t *tips = xmalloc(((n > 0U) ? n : 1U) * sizeof *tips);
    Str_t ref = STR_INIT;
    const char *p;
    char hex[OID_HEX_SIZE];
    Oid_t t;
    Oid_t have;
    eMsgId_t what;
    size_t k;

    printf("    %-6s ", msg(MSG_STATUS_FIELD_PEERS));
    if (n == 0U) {
        puts(msg(MSG_BRANCH_NONE));
        goto exit;
    }
    peers_check(e->id, &e->peers, state);
    peers_tips(e->id, &e->peers, tips);
    chain_ref(&ref, e->id);
    if (git_batch_query(b, str_cstr(&ref), &t) != GIT_OBJ_COMMIT) {
        t.len = 0U;
    }
    for (k = 0U, p = str_znext(&e->peers, NULL); p != NULL;
         k++, p = str_znext(&e->peers, p)) {
        if (state[k] == PEER_GONE) {
            what = MSG_PEER_GONE;
        } else if (state[k] == PEER_LEFT) {
            what = MSG_PEER_LEFT;
        } else if (state[k] == PEER_UNREACHABLE) {
            what = MSG_PEER_UNREACHABLE;
        } else if (tips[k].len == 0U) {
            what = MSG_PEER_NO_VERSION;
        } else if (oid_eq(&tips[k], &t)) {
            what = MSG_PEER_SAME;
        } else {
            /* Il peer ha una versione che qui non c'e', o che non discende
             * da quella del peer: serve update. Se e' un antenato di T, al
             * peer manca l'ultima versione: serve publish. */
            oid_to_hex(&tips[k], hex);
            what = ((t.len > 0U) &&
                    (git_batch_query(b, hex, &have) == GIT_OBJ_COMMIT) &&
                    merge_is_ancestor(&tips[k], &t))
                       ? MSG_PEER_BEHIND
                       : MSG_PEER_NEWER;
        }
        if (k > 0U) {
            printf("    %-6s ", "");
        }
        printf("%s: %s\n", p, msg(what));
    }

exit:
    str_free(&ref);
    free(tips);
    free(state);
}

int cmd_status(int argc, char **argv)
{
    Repo_t repo = REPO_INIT;
    GitBatch_t batch;
    BranchNames_t names = BRANCHNAMES_INIT;
    Str_t list = STR_INIT;
    Str_t elsewhere = STR_INIT; /* branch che dichiara il file (B-16) */
    Str_t alt_path = STR_INIT;
    const SharedEntry_t *e;
    eState_t st;
    size_t i;
    size_t shown = 0U;
    int batch_open = 0;
    int found;
    int suspended;
    int with_peers = 0;
    int rc = GS_EXIT_FATAL;
    eGsErr_t err;

    for (i = 1U; i < (size_t)argc; i++) {
        if (strcmp(argv[i], "--peers") == 0) {
            with_peers = 1;
        } else {
            if (argv[i][0] == '-') {
                msg_error(MSG_ERR_UNKNOWN_OPT, argv[i]);
            } else {
                msg_error(MSG_ERR_TOO_MANY_ARGS);
            }
            fputs(msg(MSG_USAGE_STATUS), stderr);
            rc = GS_EXIT_USAGE;
            goto exit;
        }
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
    repo_check_self(&repo);

    for (i = 0U; i < repo.n; i++) {
        e = &repo.v[i];
        if (e->in_gitshared && !e->valid) {
            msg_warning(MSG_WARN_BAD_ENTRY, str_cstr(&e->name));
        } else if (e->in_gitshared || e->in_config) {
            if (state_derive(&repo, e, &batch, &st, NULL) != GS_OK) {
                msg_fatal(MSG_ERR_PROC, "git");
                rc = GS_EXIT_FATAL;
                goto exit;
            }
            suspended = (state_is_derived(st) &&
                         !shared_is_active(e, str_cstr(&repo.branch))) ||
                        (st == STATE_ABSENT);
            found = 0;
            if ((st == STATE_ABSENT) &&
                (repo_declaring_branch(e->id, &batch, &elsewhere, &alt_path,
                                       &found) != GS_OK)) {
                found = 0; /* solo per i messaggi: senza, niente nota */
            }
            if (shown > 0U) {
                putchar('\n');
            }
            print_entry(&repo, e, st, suspended,
                        (found != 0) ? str_cstr(&alt_path) : NULL);
            /* Non dichiarato qui: dove lo e' (B-16, D-095). */
            if (found != 0) {
                printf("    %-6s ", msg(MSG_STATUS_FIELD_NOTE));
                printf(msg(MSG_STATUS_ELSEWHERE), str_cstr(&elsewhere));
                putchar('\n');
            }
            /* Sospeso perche' nessun suo branch esiste piu' (B-21). */
            if (suspended && shared_branches_gone(&names, e, &list)) {
                printf("    %-6s ", msg(MSG_STATUS_FIELD_NOTE));
                printf(msg(MSG_STATUS_BRANCHES_GONE), str_cstr(&list),
                       e->in_gitshared ? str_cstr(&e->path) : e->id);
                putchar('\n');
            }
            /* Peer: per i file collegati e non in attesa di set o link. */
            if (with_peers && e->in_config && (e->pending != SHARED_PENDING_SET) &&
                (e->pending != SHARED_PENDING_LINK)) {
                print_peers(e, &batch);
            }
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
    branchnames_free(&names);
    str_free(&alt_path);
    str_free(&elsewhere);
    str_free(&list);
    return rc;
}
