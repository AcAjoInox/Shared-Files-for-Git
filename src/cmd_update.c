/*
 * cmd_update.c - "git shared update [-f] [-c | -m <msg>] [<path>]"
 * (REQ-F-009, REQ-F-010, D-040).
 *
 * Per ogni file shared (o quello indicato):
 *   1. fetch dai peer, in parallelo, in ref temporanee;
 *   2. se una versione ricevuta discende da T, T avanza in fast-forward
 *      (merge-base --independent: un solo processo per tutte le versioni);
 *   3. se lo stato e' "da applicare", T va nel working tree e nell'index.
 * Non committa, salvo -c/-m (D-007). Modifiche locali: rifiuta, salvo -f.
 *
 * v0 (M2): il merge delle divergenze (REQ-F-011) non c'e' ancora.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chain.h"
#include "cmd.h"
#include "cmd_common.h"
#include "mem.h"
#include "peers.h"
#include "state.h"

/*
 * Riceve le versioni dei peer e avanza T se possibile. *diverged = 1 se le
 * versioni non sono in fila (nessuna discende da tutte le altre).
 */
static eGsErr_t receive(const SharedEntry_t *e, GitBatch_t *b, int *advanced,
                        int *diverged)
{
    const char *label = str_cstr(&e->path);
    size_t n = str_zcount(&e->peers);
    uint8_t *ok = xmalloc((n > 0U) ? n : 1U);
    PeerTally_t tally = { 0U, 0U, 0U };
    Str_t ref = STR_INIT;
    Str_t incoming = STR_INIT;
    Str_t fetched = STR_INIT; /* ref temporanee da cancellare */
    Str_t out = STR_INIT;
    Str_t argbuf = STR_INIT;  /* oid esadecimali degli argomenti */
    const char **args = NULL;
    const char *h;
    char hex[OID_HEX_SIZE];
    Oid_t t;
    Oid_t in;
    Oid_t top;
    size_t k;
    size_t na = 0U;
    size_t lines = 0U;
    eGsErr_t err = GS_OK;

    *advanced = 0;
    *diverged = 0;
    if (n == 0U) {
        goto exit;
    }
    peers_fetch(label, e->id, &e->peers, ok, &tally);

    chain_ref(&ref, e->id);
    if (git_batch_query(b, str_cstr(&ref), &t) != GIT_OBJ_COMMIT) {
        err = GS_ERR_GIT;
        goto exit;
    }
    /* merge-base --independent T I1 I2 ...: restano le versioni che nessuna
     * altra contiene. Una sola = in fila; se diversa da T, si avanza. */
    args = xmalloc((n + 4U) * sizeof *args);
    args[na++] = "merge-base";
    args[na++] = "--independent";
    oid_to_hex(&t, hex);
    str_addz(&argbuf, hex);
    for (k = 0U; k < n; k++) {
        if (ok[k] != 0U) {
            peers_incoming_ref(&incoming, e->id, (unsigned)k);
            str_addz(&fetched, str_cstr(&incoming));
            if ((git_batch_query(b, str_cstr(&incoming), &in) == GIT_OBJ_COMMIT) &&
                !oid_eq(&in, &t)) {
                oid_to_hex(&in, hex);
                str_addz(&argbuf, hex);
            }
        }
    }
    if (str_zcount(&argbuf) > 1U) {
        for (h = str_znext(&argbuf, NULL); h != NULL; h = str_znext(&argbuf, h)) {
            args[na++] = h;
        }
        args[na] = NULL;
        err = git_run(args, 0U, &out);
        if (err != GS_OK) {
            goto exit;
        }
        for (h = str_cstr(&out); *h != '\0'; h++) {
            lines += (*h == '\n') ? 1U : 0U;
        }
        lines++;
        if (lines > 1U) {
            *diverged = 1; /* versioni divergenti tra i peer (REQ-F-011) */
        } else if (oid_from_hex(&top, out.buf, out.len) && !oid_eq(&top, &t)) {
            err = git_update_ref(str_cstr(&ref), &top, &t, "git shared: update");
            *advanced = (err == GS_OK);
        } else {
            /* i peer sono indietro o uguali */
        }
    }

exit:
    if (fetched.len > 0U) {
        (void)git_delete_refs(&fetched);
    }
    free((void *)args);
    str_free(&argbuf);
    str_free(&out);
    str_free(&fetched);
    str_free(&incoming);
    str_free(&ref);
    free(ok);
    return err;
}

/* Applica T al file (D-042); rifiuta se ci sono modifiche locali. */
static int apply(const Repo_t *r, const SharedEntry_t *e, GitBatch_t *b,
                 int force, Str_t *applied)
{
    const char *label = str_cstr(&e->path);
    Str_t name = STR_INIT;
    Oid_t t;
    int ok = 0;

    if (!force && cmd_has_local_changes(r, label)) {
        msg_error(MSG_ERR_LOCAL_CHANGES, label); /* REQ-F-010, D-022 */
        goto exit;
    }
    str_addf(&name, "refs/shared/%s:content", e->id);
    if ((git_batch_blob(b, str_cstr(&name), &t) == 0) ||
        (cmd_write_file(r, label, &t) != GS_OK)) {
        msg_error(MSG_ERR_WRITE_FILE, label);
        goto exit;
    }
    str_addz(applied, label); /* il messaggio dopo l'eventuale commit */
    ok = 1;

exit:
    str_free(&name);
    return ok;
}

int cmd_update(int argc, char **argv)
{
    CmdOpts_t o;
    Repo_t repo = REPO_INIT;
    GitBatch_t batch;
    const SharedEntry_t *e;
    const char *label;
    unsigned char *sel = NULL;
    Str_t applied = STR_INIT;
    Str_t text = STR_INIT;
    const char *p;
    eState_t st;
    size_t i;
    int committed = 0;
    int first;
    int advanced;
    int diverged;
    int opened = 0;
    int rc = GS_EXIT_USAGE;

    first = cmd_parse(argc, argv, CMD_OPT_COMMIT | CMD_OPT_FORCE,
                      MSG_USAGE_UPDATE, &o);
    if (first < 0) {
        goto exit;
    }
    if ((argc - first) > 1) {
        msg_error(MSG_ERR_TOO_MANY_ARGS);
        fputs(msg(MSG_USAGE_UPDATE), stderr);
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
        if (receive(e, &batch, &advanced, &diverged) != GS_OK) {
            msg_error(MSG_ERR_CHAIN, str_cstr(&e->name));
            rc = GS_EXIT_ERROR;
            continue;
        }
        if (advanced != 0) {
            printf(msg(MSG_UPD_RECEIVED), label);
            putchar('\n');
        }
        if (diverged != 0) {
            msg_error(MSG_UPD_DIVERGED, label);
            rc = GS_EXIT_ERROR;
        }
        if (state_derive(&repo, e, &batch, &st) != GS_OK) {
            msg_fatal(MSG_ERR_PROC, "git");
            rc = GS_EXIT_FATAL;
            goto exit;
        }
        if (state_is_derived(st) && !shared_is_active(e, str_cstr(&repo.branch))) {
            msg_warning(MSG_WARN_SUSPENDED, label); /* atto esplicito: si fa */
        }
        if (st == STATE_ALIGNED) {
            printf(msg(MSG_UPD_UP_TO_DATE), label);
            putchar('\n');
        } else if (st == STATE_TO_APPLY) {
            if (!apply(&repo, e, &batch, o.force, &applied)) {
                rc = GS_EXIT_ERROR;
            }
        } else if (st == STATE_TO_PUBLISH) {
            printf(msg(MSG_UPD_TO_PUBLISH), label);
            putchar('\n');
        } else if ((st == STATE_DIVERGED) && (diverged == 0)) {
            msg_error(MSG_UPD_DIVERGED, label);
            rc = GS_EXIT_ERROR;
        } else if (st != STATE_DIVERGED) {
            printf(msg(MSG_SKIP_STATE), label, state_name(st));
            putchar('\n');
        } else {
            /* divergenza gia' segnalata */
        }
    }

    /* Commit dedicato dei soli file applicati (D-007). */
    if ((o.commit != 0U) && (applied.len > 0U)) {
        if (o.message == NULL) {
            str_addf(&text, msg(MSG_UPD_COMMIT_MSG),
                     (str_zcount(&applied) == 1U) ? applied.buf : "shared files");
            o.message = str_cstr(&text);
        }
        if (cmd_commit(&repo, o.message, &applied) == GS_OK) {
            committed = 1;
        } else {
            msg_error(MSG_ERR_COMMIT);
            rc = GS_EXIT_ERROR;
        }
    }
    for (p = str_znext(&applied, NULL); p != NULL; p = str_znext(&applied, p)) {
        printf(msg((committed != 0) ? MSG_UPD_COMMITTED : MSG_UPD_APPLIED), p);
        putchar('\n');
    }

exit:
    free(sel);
    if (opened != 0) {
        git_batch_close(&batch);
        repo_close(&repo);
    }
    str_free(&text);
    str_free(&applied);
    return rc;
}
