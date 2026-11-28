/*
 * cmd_pull.c - "git shared pull [-f] [--scan] [-c | -m <msg>] [<path>]"
 * (REQ-F-028, D-110).
 *
 * Come "git pull" = fetch + merge: per i file scelti con un creatore, la
 * catena del creatore in refs/shared-creator/<id> (un fetch per creatore,
 * con tutte le catene), poi update con le stesse opzioni (cmd_update_run).
 * Un file il cui creatore non risponde, o non ha piu' il file, non si
 * aggiorna: la rete l'ha chiesta l'utente, quindi e' un errore. --abort,
 * --ours e --theirs non hanno bisogno di rete: si usano con update.
 *
 * Prima dell'errore, gli eredi (D-114): la linea di successione si visita
 * (cmd_heirs_visit) e un erede che si e' incoronato si segue senza domande;
 * poi update come per gli altri file. Un'autorita' che non e' un erede, o
 * il creatore tornato, li dice l'errore, con il comando per seguirli.
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
    HeirVisit_t visit = HEIRVISIT_INIT;
    const char *path;
    const char *label;
    size_t n = 0U;
    size_t i;
    int first;
    int opened = 0;
    int batch_ok = 0;
    int rc = GS_EXIT_USAGE;

    first = cmd_parse(argc, argv, CMD_OPT_COMMIT | CMD_OPT_FORCE | CMD_OPT_SCAN,
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
    git_batch_close(&batch);
    if (git_batch_open(&batch) != GS_OK) {
        msg_fatal(MSG_ERR_PROC, "git cat-file");
        batch_ok = 0;
        rc = GS_EXIT_FATAL;
        goto exit;
    }
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
        } else if ((res[i] == (unsigned char)REMOTE_MISSING) ||
                   (res[i] == (unsigned char)REMOTE_UNREACHABLE)) {
            /* Gli eredi, prima di arrendersi (D-114). */
            if (!cmd_heirs_visit(&repo, &repo.v[i], &batch, 1, &visit)) {
                batch_ok = 0;
                rc = GS_EXIT_FATAL;
                goto exit;
            }
            if (visit.kind == HEIR_VISIT_FOLLOWED) {
                continue; /* T ha la nuova autorita': update come gli altri */
            }
            msg_error((res[i] == (unsigned char)REMOTE_MISSING)
                          ? MSG_ERR_CREATOR_MISSING
                          : MSG_ERR_CREATOR_UNREACHABLE,
                      label, auth_role(sov[i]), str_cstr(&urls[i]));
            if ((res[i] == (unsigned char)REMOTE_MISSING) &&
                (visit.kind != HEIR_VISIT_OTHER)) {
                /* AB-07: risponde, ma il file non c'e' piu': un incidente
                 * o un dirottamento, non per forza un'autorita' morta */
                msg_error(MSG_ERR_AUTH_NO_FILE, label, auth_role(sov[i]));
            }
            if (visit.kind == HEIR_VISIT_OTHER) {
                msg_error(MSG_ERR_HEIR_OTHER, label, str_cstr(&visit.where),
                          str_cstr(&visit.to), str_cstr(&visit.to), label);
            } else if (visit.kind == HEIR_VISIT_CREATOR) {
                msg_error(MSG_ERR_CREATOR_BACK, label, str_cstr(&visit.where), label);
            } else if ((visit.kind == HEIR_VISIT_NONE) && (visit.line == 0U)) {
                msg_error(MSG_ERR_HEIRS_EMPTY, label, label, label);
            } else if ((visit.kind == HEIR_VISIT_NONE) && (visit.answered == 0U)) {
                msg_error(MSG_ERR_HEIRS_SILENT, label, visit.line, label, label);
            } else if (visit.kind == HEIR_VISIT_NONE) {
                msg_error(MSG_ERR_HEIRS_STILL, label, visit.answered, visit.line,
                          label, label);
            } else {
                /* errore gia' detto */
            }
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
    heirvisit_free(&visit);
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
