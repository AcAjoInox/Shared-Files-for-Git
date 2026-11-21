/*
 * cmd_publish.c - "git shared publish [--remote <remote>] [<path>]"
 * (REQ-F-004, REQ-F-005, REQ-F-006, REQ-F-008; in remoto D-104).
 *
 * Per ogni file shared (o quello indicato), prima si riceve (D-099), poi:
 *   da pubblicare -> nuova versione con genitore T (la base e' T, D-065),
 *                    poi avviso ai peer;
 *   allineato     -> avviso di T ai peer (es. peer prima irraggiungibili:
 *                    un avviso che c'e' gia' non si riscrive);
 *   altri stati   -> niente: si dice cosa fare.
 * Una versione ricevuta che non discende da T va prima unita (update).
 * Ai peer con .git file si fa il push, che non forza mai (D-040).
 *
 * In remoto (D-104):
 *   - file senza creatore e --remote <remote>: la repo remota diventa il
 *     creatore. Nasce una versione con la voce "creator" (D-103), che va
 *     prima nella repo remota e solo dopo, se il push riesce, nella catena
 *     locale; poi l'avviso ai peer locali, che la ricevono come ogni altra.
 *   - file con creatore: dopo l'avviso ai peer locali, push della sola
 *     catena verso il proprio remote (--remote, o origin: una copia, per
 *     chi clona questa repo) e verso il creatore (la versione ufficiale).
 *     Il creatore accetta solo un fast-forward e solo da chi ha i permessi:
 *     se rifiuta, la versione resta qui come proposta (ideeRemote §4.7).
 *   - senza creatore e senza --remote: solo locale, come prima.
 */
#include <stdio.h>
#include <stdlib.h>

#include "chain.h"
#include "cmd.h"
#include "cmd_common.h"
#include "mem.h"
#include "receive.h"
#include "remote.h"
#include "state.h"

/*
 * Primo publish in remoto (D-104): la versione con il creatore va prima
 * nella repo remota, poi nella catena locale. Se il push non riesce non
 * cambia nulla: la repo remota non e' diventata il creatore. Contenuto: H
 * se il file e' da pubblicare, altrimenti quello di T.
 */
static int become_creator(const Repo_t *r, const SharedEntry_t *e,
                          GitBatch_t *b, eState_t st, const StateInfo_t *si,
                          const Str_t *url)
{
    const char *label = str_cstr(&e->path);
    const Oid_t *blob = (st == STATE_TO_PUBLISH) ? &si->h : &si->t;
    Str_t ref = STR_INIT;
    Oid_t creator;
    Oid_t v;
    int rc = GS_EXIT_ERROR;

    if ((remote_url_blob(str_cstr(url), &creator) != GS_OK) ||
        (chain_commit(blob, &si->tip, NULL, &creator, str_cstr(&e->name), &v) !=
         GS_OK)) {
        msg_error(MSG_ERR_PUBLISH, label);
        goto exit;
    }
    if (remote_push(str_cstr(url), e->id, &v) != REMOTE_PUSH_OK) {
        msg_error(MSG_ERR_CREATOR_PUSH, label, str_cstr(url));
        goto exit;
    }
    /* Condizionata a T. Se fallisce (T cambiata tra lo stato e qui) la
     * versione e' nella repo remota ma non nella catena locale: caso raro,
     * noto del prototipo (ideeRemote §9). */
    chain_ref(&ref, e->id);
    if (git_update_ref(str_cstr(&ref), &v, &si->tip, "git shared: publish") !=
        GS_OK) {
        msg_error(MSG_ERR_PUBLISH, label);
        goto exit;
    }
    if (st == STATE_TO_PUBLISH) {
        printf(msg(MSG_PUB_CREATED), label);
        putchar('\n');
    }
    printf(msg(MSG_PUB_CREATOR_NEW), label, str_cstr(url));
    putchar('\n');
    rc = cmd_publish_entry(r, e, b, NULL, &v); /* avviso ai peer locali */

exit:
    str_free(&ref);
    return rc;
}

/*
 * Push della catena a un file con creatore (D-104): copia nel proprio
 * remote (own, "" = nessuno), poi la versione ufficiale nel creatore. Se
 * il creatore ha gia' T (lo ha detto ls-remote alla ricezione) il push non
 * si fa: un lettore senza permessi non riceve un rifiuto per nulla.
 */
static int push_remote(const SharedEntry_t *e, const Oid_t *tip,
                       const Str_t *own, const Str_t *creator,
                       const Oid_t *creator_tip)
{
    const char *label = str_cstr(&e->path);
    int rc = GS_EXIT_OK;

    if ((own->len > 0U) && !str_eq(own, str_cstr(creator))) {
        if (remote_push(str_cstr(own), e->id, tip) == REMOTE_PUSH_OK) {
            printf(msg(MSG_PUB_COPIED), label, str_cstr(own));
            putchar('\n');
        } else {
            msg_warning(MSG_WARN_COPY_FAILED, label, str_cstr(own));
        }
    }
    if ((creator_tip->len > 0U) && oid_eq(creator_tip, tip)) {
        printf(msg(MSG_PUB_CREATOR_HAS), label, str_cstr(creator));
        putchar('\n');
    } else {
        switch (remote_push(str_cstr(creator), e->id, tip)) {
        case REMOTE_PUSH_OK:
            printf(msg(MSG_PUB_TO_CREATOR), label, str_cstr(creator));
            putchar('\n');
            break;
        case REMOTE_PUSH_NEWER:
            msg_error(MSG_ERR_CREATOR_NEWER, label, str_cstr(creator));
            rc = GS_EXIT_ERROR;
            break;
        default:
            msg_error(MSG_ERR_CREATOR_REFUSED, label, str_cstr(creator));
            rc = GS_EXIT_ERROR;
            break;
        }
    }
    return rc;
}

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
    Str_t own = STR_INIT;     /* URL del proprio remote, "" = nessuno */
    Str_t creator = STR_INIT; /* URL del creatore di T, "" = nessuno */
    Oid_t tip;
    size_t i;
    int first;
    int opened = 0;
    int rc = GS_EXIT_USAGE;

    first = cmd_parse(argc, argv, CMD_OPT_REMOTE, MSG_USAGE_PUBLISH, &o);
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
    /* Il proprio remote: quello dato, che deve esistere, o origin se c'e'. */
    if (o.remote != NULL) {
        if (remote_url(o.remote, &own) != GS_OK) {
            msg_error(MSG_ERR_NO_REMOTE, o.remote);
            rc = GS_EXIT_ERROR;
            goto exit;
        }
    } else {
        (void)remote_url("origin", &own);
    }
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
        /* Prima si riceve (D-099): una versione nuova di un peer o del
         * creatore cambia lo stato. Una catena assente la segnala lo
         * stato. */
        (void)receive_entry(&repo, e, &batch, 0, &rv);
        if (rv.creator_unreachable != 0) {
            msg_warning(MSG_WARN_CREATOR_UNREACHABLE, label, str_cstr(&rv.creator));
        }
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
            /* Il creatore di T (dopo la ricezione: T puo' essere avanzata
             * su una versione arrivata in remoto). */
            if (remote_creator_url(&batch, &si.tip, &creator) != GS_OK) {
                str_reset(&creator);
            }
            if ((creator.len == 0U) && (o.remote != NULL)) {
                if (become_creator(&repo, e, &batch, st, &si, &own) != GS_EXIT_OK) {
                    rc = GS_EXIT_ERROR;
                }
            } else {
                tip = si.tip;
                if (cmd_publish_entry(&repo, e, &batch,
                                      (st == STATE_TO_PUBLISH) ? &si : NULL,
                                      &tip) != GS_EXIT_OK) {
                    rc = GS_EXIT_ERROR;
                } else if ((creator.len > 0U) &&
                           (push_remote(e, &tip, &own, &creator,
                                        &rv.creator_tip) != GS_EXIT_OK)) {
                    rc = GS_EXIT_ERROR;
                } else {
                    /* pubblicato */
                }
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
    str_free(&creator);
    str_free(&own);
    return rc;
}
