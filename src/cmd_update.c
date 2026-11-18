/*
 * cmd_update.c - "git shared update [-f] [--all] [-c | -m <msg>] [<path>]"
 * (REQ-F-009, REQ-F-010, REQ-F-011, D-040, D-078, D-099).
 *
 * Per ogni file shared (o quello indicato):
 *   1. ricezione (receive.h): fetch solo dalle repo che hanno annunciato
 *      una versione che qui manca, da tutti i peer con --all; se una
 *      versione ricevuta discende da T, T avanza in fast-forward; le
 *      versioni che non discendono da T restano da unire;
 *   2. un'unione in corso con la risoluzione in staging viene conclusa;
 *   3. il branch va a T: "da applicare" -> T nel working tree e
 *      nell'index; "divergente" -> unione a 3 vie (base, H, T);
 *   4. le versioni dei peer rimaste si uniscono una alla volta.
 * Ogni unione pulita crea la versione nella catena prima del commit:
 * dopo, il file e' "da committare" (D-076). Al primo conflitto ci si
 * ferma: stage nell'index, marker nel file, refs/shared-merge/<id>.
 * Non committa, salvo -c/-m (D-007, D-075). Modifiche locali: rifiuta,
 * salvo -f; una versione applicata e non committata non lo e' (D-076).
 *
 * "git shared update --abort [<path>]" (D-093): rinuncia all'unione in
 * corso, senza fetch; il file torna quello di HEAD.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chain.h"
#include "cmd.h"
#include "cmd_common.h"
#include "mem.h"
#include "merge.h"
#include "peers.h"
#include "receive.h"
#include "state.h"

/* Esito di un passo di unione. */
typedef enum {
    STEP_DONE = 0, /* risultato nell'index e nel working tree */
    STEP_CONFLICT, /* conflitto lasciato all'utente */
    STEP_ERROR
} eStep_t;

/* Elenchi dei file toccati, per il commit e i messaggi finali. */
typedef struct {
    Str_t applied; /* da committare (o committati con -c) */
    Str_t merged;  /* tra questi, quelli nati da un'unione */
} Touched_t;

static void add_once(Str_t *list, const char *label)
{
    if (!str_zhas(list, label)) {
        str_addz(list, label);
    }
}

/* Un cat-file nuovo, che veda l'index appena modificato. */
static eGsErr_t refresh(GitBatch_t *b, int *batch_ok)
{
    eGsErr_t err;

    git_batch_close(b);
    err = git_batch_open(b);
    if (err != GS_OK) {
        msg_fatal(MSG_ERR_PROC, "git cat-file");
        *batch_ok = 0;
    }
    return err;
}

/* Blob di una versione: "<commit>:content". */
static int version_blob(GitBatch_t *b, const Oid_t *commit, Oid_t *blob)
{
    char hex[OID_HEX_SIZE];
    Str_t name = STR_INIT;
    int ok;

    oid_to_hex(commit, hex);
    str_addf(&name, "%s:content", hex);
    ok = git_batch_blob(b, str_cstr(&name), blob);
    str_free(&name);
    return ok;
}

/* Toglie refs/shared-merge/<id>, se c'e'. */
static void drop_marker(const SharedEntry_t *e, GitBatch_t *b)
{
    Str_t ref = STR_INIT;
    Oid_t k;

    merge_ref(&ref, e->id);
    if (git_batch_query(b, str_cstr(&ref), &k) == GIT_OBJ_COMMIT) {
        (void)merge_drop(e->id);
    }
    str_free(&ref);
}

/*
 * Un passo di unione (D-078): contenuto `ours` (quello del branch o
 * dell'index) con la versione di blob `theirs`, partendo da `base` (NULL =
 * nessuna base: base vuota, D-029).
 *
 * Se riesce, il risultato e' nell'index e nel working tree (*result) e,
 * se porta qualcosa di nuovo, diventa una versione con genitori *tip e p2
 * (NULL = uno solo): la ref avanza solo se vale ancora *tip, che viene
 * aggiornato. Un risultato uguale a `tip_blob` senza secondo genitore e'
 * la versione che c'e' gia': nessuna versione nuova.
 *
 * In conflitto: refs/shared-merge/<id> registra *tip e p2, le versioni
 * che la risoluzione incorporera'.
 */
static eStep_t merge_step(const Repo_t *r, const SharedEntry_t *e,
                          GitBatch_t *b, const Oid_t *base, const Oid_t *ours,
                          const Oid_t *theirs, const Oid_t *tip_blob,
                          const Oid_t *p2, Oid_t *tip, Oid_t *result)
{
    const char *top = str_cstr(&r->top);
    const char *path = str_cstr(&e->path);
    Str_t ref = STR_INIT;
    Oid_t empty;
    Oid_t v;
    int conflict = 0;
    eGsErr_t err = GS_OK;
    eStep_t step = STEP_ERROR;

    /* 1. Il contenuto. I casi senza unione vera non passano dagli stage. */
    if (((base != NULL) && oid_eq(theirs, base)) || oid_eq(ours, theirs)) {
        *result = *ours; /* niente di nuovo dall'altra parte */
        err = cmd_write_file(r, path, result);
    } else if ((base != NULL) && oid_eq(ours, base)) {
        *result = *theirs; /* niente di nostro: e' un'applicazione */
        err = cmd_write_file(r, path, result);
    } else {
        if (base == NULL) {
            err = merge_empty_blob(&empty);
            base = &empty;
        }
        if (err == GS_OK) {
            err = merge_three(top, path, base, ours, theirs, &conflict);
        }
        if ((err == GS_OK) && (conflict == 0)) {
            err = merge_index_blob(top, path, result);
        }
    }
    if (err != GS_OK) {
        msg_error(MSG_ERR_MERGE, path);
        goto exit;
    }

    /* 2. Conflitto: l'unione resta registrata. Il commit provvisorio ha
     * per genitori le versioni che si stanno unendo. */
    if (conflict != 0) {
        merge_ref(&ref, e->id);
        if ((chain_commit(theirs, tip, p2, str_cstr(&e->name), &v) != GS_OK) ||
            (git_set_ref(str_cstr(&ref), &v, "git shared: merge") != GS_OK)) {
            msg_error(MSG_ERR_MERGE, path);
            goto exit;
        }
        msg_error(MSG_ERR_CONFLICT_OPEN, path, path);
        step = STEP_CONFLICT;
        goto exit;
    }

    /* 3. Unione pulita: la versione nasce prima del commit, cosi' dopo lo
     * stato e' "da committare" senza altri dati salvati. */
    if ((p2 != NULL) || !oid_eq(result, tip_blob)) {
        chain_ref(&ref, e->id);
        if ((chain_commit(result, tip, p2, str_cstr(&e->name), &v) != GS_OK) ||
            (git_update_ref(str_cstr(&ref), &v, tip, "git shared: merge") != GS_OK)) {
            /* es. un peer ha inviato una versione nel frattempo: il
             * risultato resta nell'index, il prossimo update riparte */
            msg_error(MSG_ERR_CHAIN, str_cstr(&e->name));
            goto exit;
        }
        *tip = v;
    }
    drop_marker(e, b);
    step = STEP_DONE;

exit:
    str_free(&ref);
    return step;
}

/*
 * Unione registrata in refs/shared-merge/<id>. Ritorna 1 se si puo'
 * proseguire con lo stato (nessuna unione, o conclusa), 0 se il file e'
 * da saltare (*rc aggiornato).
 */
static int settle_merge(const Repo_t *r, const SharedEntry_t *e, GitBatch_t *b,
                        const CmdOpts_t *o, Touched_t *touched, int *rc,
                        int *batch_ok)
{
    const char *label = str_cstr(&e->path);
    char hex[OID_HEX_SIZE];
    Str_t name = STR_INIT;
    Oid_t k;      /* commit provvisorio dell'unione */
    Oid_t old;    /* T all'inizio dell'unione */
    Oid_t p2;     /* versione del peer, se l'unione era tra versioni */
    Oid_t h;
    Oid_t i;
    Oid_t base;
    Oid_t tip;
    Oid_t t;
    Oid_t result;
    int unmerged = 0;
    int has_p2;
    int go_on = 0;
    eStep_t step;

    merge_ref(&name, e->id);
    if (git_batch_query(b, str_cstr(&name), &k) != GIT_OBJ_COMMIT) {
        go_on = 1; /* nessuna unione in corso */
        goto exit;
    }
    str_reset(&name);
    str_addf(&name, "HEAD:%s", label);
    if ((git_batch_blob(b, str_cstr(&name), &h) == 0) ||
        !merge_is_live(b, label, &h, &unmerged)) {
        /* Abbandonata (git restore): l'index e' tornato a HEAD. */
        drop_marker(e, b);
        msg_warning(MSG_WARN_MERGE_DROPPED, label);
        go_on = 1;
        goto exit;
    }
    if (unmerged != 0) {
        msg_error(MSG_ERR_CONFLICT_OPEN, label, label);
        *rc = GS_EXIT_ERROR;
        goto exit;
    }
    /* Risoluzione in staging: e' lei il contenuto da unire. */
    if ((o->force == 0U) && cmd_worktree_differs(r, label)) {
        msg_error(MSG_ERR_LOCAL_CHANGES, label);
        *rc = GS_EXIT_ERROR;
        goto exit;
    }
    oid_to_hex(&k, hex);
    str_reset(&name);
    str_addf(&name, "%s^1", hex);
    if (git_batch_query(b, str_cstr(&name), &old) != GIT_OBJ_COMMIT) {
        msg_error(MSG_ERR_MERGE, label);
        *rc = GS_EXIT_ERROR;
        goto exit;
    }
    str_reset(&name);
    str_addf(&name, "%s^2", hex);
    has_p2 = (git_batch_query(b, str_cstr(&name), &p2) == GIT_OBJ_COMMIT);
    chain_ref(&name, e->id);
    if ((git_batch_query(b, str_cstr(&name), &tip) != GIT_OBJ_COMMIT) ||
        !version_blob(b, &tip, &t) || !version_blob(b, &old, &base) ||
        (merge_index_blob(str_cstr(&r->top), label, &i) != GS_OK)) {
        msg_error(MSG_ERR_MERGE, label);
        *rc = GS_EXIT_ERROR;
        goto exit;
    }
    /* La risoluzione incorpora `old` (e p2). Se T e' ancora `old` diventa
     * la nuova versione cosi' com'e'; se nel frattempo e' avanzata
     * (reportIdea B-17) e' un'altra unione, con base `old`. */
    step = merge_step(r, e, b, &base, &i, &t, &t, has_p2 ? &p2 : NULL, &tip,
                      &result);
    if (refresh(b, batch_ok) != GS_OK) {
        *rc = GS_EXIT_FATAL;
        goto exit;
    }
    if (step != STEP_DONE) {
        *rc = GS_EXIT_ERROR;
        goto exit;
    }
    add_once(&touched->merged, label);
    go_on = 1;

exit:
    str_free(&name);
    return go_on;
}

/* Applica T al file (D-042); rifiuta se ci sono modifiche locali. */
static int apply(const Repo_t *r, const SharedEntry_t *e, GitBatch_t *b,
                 const StateInfo_t *si, int force)
{
    const char *label = str_cstr(&e->path);
    int ok = 0;

    if (!force && cmd_has_local_changes(r, e, b)) {
        msg_error(MSG_ERR_LOCAL_CHANGES, label); /* REQ-F-010, D-022 */
    } else if (cmd_write_file(r, label, &si->t) != GS_OK) {
        msg_error(MSG_ERR_WRITE_FILE, label);
    } else {
        ok = 1;
    }
    return ok;
}

/* Un file: ricezione, unione in corso, stato, unioni. Ritorna 0 solo se
 * il comando non puo' proseguire (*rc = GS_EXIT_FATAL). */
static int update_entry(const Repo_t *r, const SharedEntry_t *e, GitBatch_t *b,
                        const CmdOpts_t *o, Touched_t *touched, int *rc,
                        int *batch_ok, BranchNames_t *bn)
{
    const char *label = str_cstr(&e->path);
    Received_t rv = RECEIVED_INIT; /* versioni dei peer da unire: rv.heads */
    StateInfo_t si;
    eState_t st;
    Oid_t cur;    /* contenuto attuale dell'index, se e' noto */
    Oid_t tip;
    Oid_t mb;
    Oid_t base;
    Oid_t theirs;
    Oid_t result;
    size_t k;
    int was_merged;
    int go_on = 1;
    eStep_t step;

    cur.len = 0U;
    if (receive_entry(r, e, b, (int)o->all, &rv) != GS_OK) {
        msg_error(MSG_ERR_CHAIN, str_cstr(&e->name));
        *rc = GS_EXIT_ERROR;
        goto exit;
    }
    if (rv.advanced != 0) {
        printf(msg(MSG_UPD_RECEIVED), label);
        putchar('\n');
    }
    if (!settle_merge(r, e, b, o, touched, rc, batch_ok)) {
        go_on = (*batch_ok != 0);
        goto exit;
    }
    was_merged = str_zhas(&touched->merged, label);
    if (state_derive(r, e, b, &st, &si) != GS_OK) {
        msg_fatal(MSG_ERR_PROC, "git");
        *rc = GS_EXIT_FATAL;
        go_on = 0;
        goto exit;
    }
    if (state_is_derived(st) && !shared_is_active(e, str_cstr(&r->branch))) {
        cmd_warn_suspended(e, bn); /* atto esplicito: si fa */
    }
    tip = si.tip;

    /* --- il branch va a T ------------------------------------------------ */
    if (st == STATE_ALIGNED) {
        cur = si.t;
        if (was_merged) {
            printf(msg(MSG_UPD_MERGED_PUBLISH), label);
            putchar('\n');
        } else if (rv.heads.n == 0U) {
            printf(msg(MSG_UPD_UP_TO_DATE), label);
            putchar('\n');
        } else {
            /* allineato a T, ma i peer hanno altro: si unisce sotto */
        }
    } else if (st == STATE_TO_APPLY) {
        if (apply(r, e, b, &si, (int)o->force)) {
            add_once(&touched->applied, label);
            cur = si.t;
        } else {
            *rc = GS_EXIT_ERROR;
        }
    } else if (st == STATE_TO_COMMIT) {
        /* Gia' applicata o unita (D-076): con -c si committa, se il
         * working tree e' ancora quello (--only committa il WT). */
        cur = si.t;
        if (o->commit == 0U) {
            if (rv.heads.n == 0U) {
                printf(msg(was_merged ? MSG_UPD_MERGED : MSG_UPD_TO_COMMIT),
                       label);
                putchar('\n');
            }
        } else if (cmd_worktree_differs(r, label)) {
            msg_error(MSG_ERR_LOCAL_CHANGES, label);
            *rc = GS_EXIT_ERROR;
            cur.len = 0U;
        } else {
            add_once(&touched->applied, label);
        }
    } else if (st == STATE_TO_PUBLISH) {
        cur = si.h;
        if (rv.heads.n == 0U) {
            printf(msg(MSG_UPD_TO_PUBLISH), label);
            putchar('\n');
        }
    } else if (st == STATE_DIVERGED) {
        /* Modifica committata da una base vecchia: base, H, T. */
        if ((o->force == 0U) && cmd_has_local_changes(r, e, b)) {
            msg_error(MSG_ERR_LOCAL_CHANGES, label);
            *rc = GS_EXIT_ERROR;
        } else {
            step = merge_step(r, e, b, (si.base.len > 0U) ? &si.base : NULL,
                              &si.h, &si.t, &si.t, NULL, &tip, &result);
            if (refresh(b, batch_ok) != GS_OK) {
                *rc = GS_EXIT_FATAL;
                go_on = 0;
                goto exit;
            }
            if (step != STEP_DONE) {
                *rc = GS_EXIT_ERROR;
            } else {
                cur = result;
                add_once(&touched->merged, label);
                if (oid_eq(&result, &si.h)) {
                    /* H conteneva gia' T (es. dopo uno squash) */
                    printf(msg(MSG_UPD_MERGED_PUBLISH), label);
                    putchar('\n');
                } else {
                    add_once(&touched->applied, label);
                }
            }
        }
    } else if (st == STATE_MERGING) {
        msg_error(MSG_ERR_CONFLICT_OPEN, label, label);
        *rc = GS_EXIT_ERROR;
    } else {
        printf(msg(MSG_SKIP_STATE), label, state_name(st));
        putchar('\n');
    }

    /* --- versioni dei peer che non discendono da T: una alla volta ------- */
    for (k = 0U; (k < rv.heads.n) && (cur.len > 0U); k++) {
        if (merge_base(&tip, &rv.heads.v[k], &mb) != GS_OK) {
            msg_error(MSG_ERR_MERGE, label);
            *rc = GS_EXIT_ERROR;
            break;
        }
        /* Gia' contenuta in T: la conclusione di un'unione in corso
         * (settle_merge) l'ha appena unita. Unirla di nuovo creerebbe una
         * seconda versione con lo stesso contenuto (reportIdea B-49). */
        if ((mb.len > 0U) && oid_eq(&mb, &rv.heads.v[k])) {
            continue;
        }
        if ((o->force == 0U) && cmd_has_local_changes(r, e, b)) {
            msg_error(MSG_ERR_LOCAL_CHANGES, label);
            *rc = GS_EXIT_ERROR;
            break;
        }
        if (!version_blob(b, &rv.heads.v[k], &theirs) ||
            ((mb.len > 0U) && !version_blob(b, &mb, &base))) {
            msg_error(MSG_ERR_MERGE, label);
            *rc = GS_EXIT_ERROR;
            break;
        }
        step = merge_step(r, e, b, (mb.len > 0U) ? &base : NULL, &cur, &theirs,
                          &cur, &rv.heads.v[k], &tip, &result);
        if (refresh(b, batch_ok) != GS_OK) {
            *rc = GS_EXIT_FATAL;
            go_on = 0;
            goto exit;
        }
        if (step != STEP_DONE) {
            *rc = GS_EXIT_ERROR;
            break;
        }
        cur = result;
        add_once(&touched->merged, label);
        if (oid_eq(&result, &si.h)) {
            printf(msg(MSG_UPD_MERGED_PUBLISH), label);
            putchar('\n');
        } else {
            add_once(&touched->applied, label);
        }
    }

exit:
    received_free(&rv);
    return go_on;
}

/*
 * --abort (D-093): rinuncia all'unione in corso del file. Il file torna
 * quello di HEAD nell'index (gli stage di conflitto spariscono) e nel
 * working tree, poi si toglie refs/shared-merge/<id>. T non cambia. In un
 * solo comando: nessun momento in cui una modifica messa in staging possa
 * passare per la risoluzione (reportIdea B-48). named = file indicato
 * dall'utente: senza unione e' un errore; altrimenti si salta.
 */
static void abort_entry(const Repo_t *r, const SharedEntry_t *e, GitBatch_t *b,
                        int named, int *rc, int *found)
{
    const char *label = str_cstr(&e->path);
    Str_t name = STR_INIT;
    Oid_t k;
    Oid_t h;

    merge_ref(&name, e->id);
    if (git_batch_query(b, str_cstr(&name), &k) != GIT_OBJ_COMMIT) {
        if (named != 0) {
            msg_error(MSG_ERR_NO_MERGE, label);
            *rc = GS_EXIT_ERROR;
        }
        goto exit;
    }
    *found = 1;
    /* Prima il file, poi la ref: se il file non si scrive l'unione resta
     * registrata e il comando si puo' ripetere. */
    str_reset(&name);
    str_addf(&name, "HEAD:%s", label);
    if ((git_batch_blob(b, str_cstr(&name), &h) == 0) ||
        (cmd_write_file(r, label, &h) != GS_OK)) {
        msg_error(MSG_ERR_WRITE_FILE, label);
        *rc = GS_EXIT_ERROR;
        goto exit;
    }
    if (merge_drop(e->id) != GS_OK) {
        /* il file e' gia' quello di HEAD: la ref e' scaduta e la toglie
         * il prossimo comando (D-092) */
        msg_error(MSG_ERR_MERGE, label);
        *rc = GS_EXIT_ERROR;
        goto exit;
    }
    printf(msg(MSG_UPD_ABORTED), label);
    putchar('\n');

exit:
    str_free(&name);
}

int cmd_update(int argc, char **argv)
{
    CmdOpts_t o;
    Repo_t repo = REPO_INIT;
    GitBatch_t batch;
    Touched_t touched = { STR_INIT, STR_INIT };
    BranchNames_t names = BRANCHNAMES_INIT;
    unsigned char *sel = NULL;
    Str_t text = STR_INIT;
    const char *p;
    eMsgId_t id;
    size_t i;
    int committed = 0;
    int aborted = 0;
    int first;
    int opened = 0;
    int batch_ok = 0;
    int rc = GS_EXIT_USAGE;

    first = cmd_parse(argc, argv,
                      CMD_OPT_COMMIT | CMD_OPT_FORCE | CMD_OPT_ABORT | CMD_OPT_ALL,
                      MSG_USAGE_UPDATE, &o);
    if (first < 0) {
        goto exit;
    }
    if ((argc - first) > 1) {
        msg_error(MSG_ERR_TOO_MANY_ARGS);
        fputs(msg(MSG_USAGE_UPDATE), stderr);
        goto exit;
    }
    if ((o.abort != 0U) && ((o.commit != 0U) || (o.force != 0U) || (o.all != 0U))) {
        msg_error(MSG_ERR_ABORT_OPTS);
        fputs(msg(MSG_USAGE_UPDATE), stderr);
        goto exit;
    }
    rc = cmd_open(&repo, &batch);
    if (rc != GS_EXIT_OK) {
        goto exit;
    }
    opened = 1;
    batch_ok = 1;
    sel = xmalloc((repo.n > 0U) ? repo.n : 1U);
    if (!cmd_select(&repo, (first < argc) ? argv[first] : NULL, sel)) {
        rc = GS_EXIT_ERROR;
        goto exit;
    }

    if (o.abort != 0U) {
        for (i = 0U; i < repo.n; i++) {
            if (sel[i] != 0U) {
                abort_entry(&repo, &repo.v[i], &batch, (first < argc), &rc,
                            &aborted);
            }
        }
        if ((aborted == 0) && (first >= argc)) {
            msg_error(MSG_ERR_NO_MERGE_ANY);
            rc = GS_EXIT_ERROR;
        }
        goto exit;
    }

    for (i = 0U; i < repo.n; i++) {
        if ((sel[i] != 0U) &&
            !update_entry(&repo, &repo.v[i], &batch, &o, &touched, &rc,
                          &batch_ok, &names)) {
            goto exit;
        }
    }

    /* Commit dedicato dei soli file applicati o uniti (D-007). */
    if ((o.commit != 0U) && (touched.applied.len > 0U)) {
        if (o.message == NULL) {
            str_addf(&text, msg(MSG_UPD_COMMIT_MSG),
                     (str_zcount(&touched.applied) == 1U) ? touched.applied.buf
                                                         : "shared files");
            o.message = str_cstr(&text);
        }
        if (cmd_commit(&repo, o.message, &touched.applied) == GS_OK) {
            committed = 1;
        } else {
            msg_error(MSG_ERR_COMMIT);
            rc = GS_EXIT_ERROR;
        }
    }
    for (p = str_znext(&touched.applied, NULL); p != NULL;
         p = str_znext(&touched.applied, p)) {
        if (str_zhas(&touched.merged, p)) {
            id = (committed != 0) ? MSG_UPD_MERGED_COMMITTED : MSG_UPD_MERGED;
        } else {
            id = (committed != 0) ? MSG_UPD_COMMITTED : MSG_UPD_APPLIED;
        }
        printf(msg(id), p);
        putchar('\n');
    }

exit:
    free(sel);
    if (opened != 0) {
        if (batch_ok != 0) {
            git_batch_close(&batch);
        }
        repo_close(&repo);
    }
    str_free(&text);
    str_free(&touched.merged);
    str_free(&touched.applied);
    branchnames_free(&names);
    return rc;
}
