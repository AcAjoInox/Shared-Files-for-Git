/*
 * cmd_relocate.c - repo spostate o copiate (REQ-F-015, REQ-F-017, D-084).
 *
 *   git shared relocate [<vecchio>]          questa repo e' stata spostata:
 *                                            ogni peer sostituisce <vecchio>
 *                                            con il path attuale
 *   git shared relocate --copy [<vecchio>]   questa repo e' una copia di
 *                                            <vecchio>: diventa un peer in
 *                                            piu', <vecchio> resta
 *   git shared relocate --peer <vecchio> <nuovo>
 *                                            un peer e' stato spostato e
 *                                            non l'ha detto: si corregge qui
 *
 * Senza <vecchio> vale il path che la repo ricorda (shared.selfPath).
 * Spostamento e copia non si distinguono dal contenuto (reportIdea B-22):
 * decide il vecchio path. Se c'e' ancora una repo e' una copia, e senza
 * --copy si rifiuta; se non c'e' piu' e' uno spostamento, e con --copy si
 * rifiuta. Effetto immediato: sono dati locali (REQ-F-023).
 */
#include <stdio.h>
#include <string.h>

#include "cmd.h"
#include "cmd_common.h"
#include "peers.h"
#include "sys.h"

typedef struct {
    const char *arg[2];
    unsigned nargs;
    unsigned copy : 1U;
    unsigned peer : 1U;
} RelocOpts_t;

static int parse_args(int argc, char **argv, RelocOpts_t *o)
{
    const char *a;
    int i;
    int end_opts = 0;
    int ok = 1;

    for (i = 1; (i < argc) && ok; i++) {
        a = argv[i];
        if (!end_opts && (strcmp(a, "--") == 0)) {
            end_opts = 1;
        } else if (!end_opts && (strcmp(a, "--copy") == 0)) {
            o->copy = 1U;
        } else if (!end_opts && (strcmp(a, "--peer") == 0)) {
            o->peer = 1U;
        } else if (!end_opts && (a[0] == '-') && (a[1] != '\0')) {
            msg_error(MSG_ERR_UNKNOWN_OPT, a);
            ok = 0;
        } else if (o->nargs >= 2U) {
            msg_error(MSG_ERR_TOO_MANY_ARGS);
            ok = 0;
        } else {
            o->arg[o->nargs++] = a;
        }
    }
    if (ok && o->copy && o->peer) {
        msg_error(MSG_ERR_UNKNOWN_OPT, "--copy --peer");
        ok = 0;
    } else if (ok && o->peer && (o->nargs != 2U)) {
        msg_error((o->nargs < 2U) ? MSG_ERR_MISSING_PATH : MSG_ERR_TOO_MANY_ARGS);
        ok = 0;
    } else if (ok && !o->peer && (o->nargs > 1U)) {
        msg_error(MSG_ERR_TOO_MANY_ARGS);
        ok = 0;
    } else {
        /* combinazione valida */
    }
    if (!ok) {
        fputs(msg(MSG_USAGE_RELOCATE), stderr);
    }
    return ok;
}

/* Voci su cui lavorare: collegate, non un set o un link in attesa. */
static int is_linked(const SharedEntry_t *e)
{
    return e->in_config && (e->pending != SHARED_PENDING_SET) &&
           (e->pending != SHARED_PENDING_LINK);
}

static const char *label_of(const SharedEntry_t *e)
{
    return e->in_gitshared ? str_cstr(&e->path) : e->id;
}

/* 1 se la repo a `path` ha la sezione del file (prune: membro). */
static int is_member(const char *id, const char *path)
{
    Str_t one = STR_INIT;
    uint8_t state = (uint8_t)PEER_GONE;

    str_addz(&one, path);
    peers_check(id, &one, &state);
    str_free(&one);
    return state == (uint8_t)PEER_MEMBER;
}

/* Questa repo: spostata (copy = 0) o copiata (copy = 1) da `old`. */
static int relocate_self(Repo_t *r, const char *old, int copy)
{
    const char *top = str_cstr(&r->top);
    const char *add[] = { "config", "--add", NULL, old, NULL };
    PeerTally_t tally;
    SharedEntry_t *e;
    Str_t key = STR_INIT;
    size_t i;
    unsigned n;
    int exists;
    int rc = GS_EXIT_ERROR;

    if (str_eq(&r->top, old)) {
        msg_error(MSG_ERR_RELOCATE_SAME, old);
        goto exit;
    }
    exists = peers_repo_exists(old);
    if (!copy && exists) {
        msg_error(MSG_ERR_RELOCATE_EXISTS, old, old); /* reportIdea B-22 */
        goto exit;
    }
    if (copy && !exists) {
        msg_error(MSG_ERR_RELOCATE_GONE, old, old);
        goto exit;
    }

    for (i = 0U; i < r->n; i++) {
        e = &r->v[i];
        if (!is_linked(e)) {
            continue;
        }
        tally.ok = 0U;
        tally.rejected = 0U;
        tally.unreachable = 0U;
        if (!copy) {
            /* Ogni peer: la voce del vecchio path diventa quella nuova. */
            peers_replace(label_of(e), e->id, &e->peers, old, top, &tally);
            printf(msg(MSG_RELOCATE_DONE), label_of(e), tally.ok,
                   (unsigned)str_zcount(&e->peers));
        } else {
            /* La repo originale diventa un peer di questa, se condivide
             * ancora il file; poi tutti i peer conoscono questa. */
            if (!str_zhas(&e->peers, old) && is_member(e->id, old)) {
                str_reset(&key);
                str_addf(&key, "shared.%s.peer", e->id);
                add[2] = str_cstr(&key);
                if (git_run(add, 0U, NULL) != GS_OK) {
                    msg_error(MSG_ERR_PENDING);
                    goto exit;
                }
                str_addz(&e->peers, old);
            }
            n = (unsigned)str_zcount(&e->peers);
            peers_announce(e->id, &e->peers, top, &tally);
            printf(msg(MSG_RELOCATE_COPY), label_of(e), tally.ok, n);
        }
        putchar('\n');
    }
    if (repo_set_self(r, top) != GS_OK) {
        msg_error(MSG_ERR_PENDING);
        goto exit;
    }
    printf(msg(MSG_RELOCATE_SELF), top);
    putchar('\n');
    rc = GS_EXIT_OK;

exit:
    str_free(&key);
    return rc;
}

/* Un peer spostato: qui la sua voce passa da `old` a `new_top`. */
static int relocate_peer(Repo_t *r, const char *old, const char *new_top)
{
    const char *args[] = { "config", "--replace-all", NULL, new_top, NULL, NULL };
    const SharedEntry_t *e;
    Str_t key = STR_INIT;
    Str_t regex = STR_INIT;
    size_t i;
    unsigned done = 0U;
    int rc = GS_EXIT_ERROR;

    if (peers_repo_exists(old)) {
        msg_error(MSG_ERR_RELOCATE_NOT_MOVED, old);
        goto exit;
    }
    peers_value_regex(&regex, old, new_top);
    args[4] = str_cstr(&regex);
    for (i = 0U; i < r->n; i++) {
        e = &r->v[i];
        if (!is_linked(e) || !str_zhas(&e->peers, old)) {
            continue;
        }
        if (!is_member(e->id, new_top)) {
            msg_warning(MSG_WARN_RELOCATE_NOT_MEMBER, label_of(e), new_top);
            continue;
        }
        str_reset(&key);
        str_addf(&key, "shared.%s.peer", e->id);
        args[2] = str_cstr(&key);
        if (git_run(args, 0U, NULL) != GS_OK) {
            msg_error(MSG_ERR_PENDING);
            goto exit;
        }
        printf(msg(MSG_RELOCATE_PEER), label_of(e), old, new_top);
        putchar('\n');
        done++;
    }
    if (done == 0U) {
        msg_error(MSG_ERR_RELOCATE_NO_PEER, old);
        goto exit;
    }
    rc = GS_EXIT_OK;

exit:
    str_free(&regex);
    str_free(&key);
    return rc;
}

int cmd_relocate(int argc, char **argv)
{
    RelocOpts_t o = { { NULL, NULL }, 0U, 0U, 0U };
    Repo_t repo = REPO_INIT;
    GitBatch_t batch;
    Str_t old = STR_INIT;
    Str_t new_top = STR_INIT;
    Str_t prefix = STR_INIT;
    Str_t objfmt = STR_INIT;
    size_t i;
    int linked = 0;
    int opened = 0;
    int rc = GS_EXIT_USAGE;
    eGsErr_t err;

    if (!parse_args(argc, argv, &o)) {
        goto exit;
    }
    /* Come cmd_open, senza l'avviso di repo spostata: e' questo il
     * comando che lo risolve. */
    rc = GS_EXIT_FATAL;
    err = repo_open(&repo);
    if (err != GS_OK) {
        msg_fatal((err == GS_ERR_NOT_REPO) ? MSG_ERR_NOT_REPO : MSG_ERR_PROC, "git");
        goto exit;
    }
    if (git_batch_open(&batch) != GS_OK) {
        msg_fatal(MSG_ERR_PROC, "git cat-file");
        goto exit;
    }
    opened = 1;
    if (cmd_complete(&repo, &batch, 1) != GS_OK) {
        msg_error(MSG_ERR_PENDING);
    }
    rc = GS_EXIT_ERROR;

    for (i = 0U; (i < repo.n) && !linked; i++) {
        linked = is_linked(&repo.v[i]);
    }
    if (!linked) {
        msg_error(MSG_ERR_RELOCATE_NOTHING);
        goto exit;
    }
    if (o.nargs > 0U) {
        if (sys_abs_path(o.arg[0], &old) != GS_OK) {
            msg_error(MSG_ERR_DEST_PATH, o.arg[0]);
            goto exit;
        }
    } else if (repo.self_path.len > 0U) {
        str_add(&old, str_cstr(&repo.self_path));
    } else {
        msg_error(MSG_ERR_RELOCATE_WHERE);
        goto exit;
    }

    if (o.peer) {
        /* Il nuovo path del peer nella forma di git (radice). Un file o
         * un path che non c'e' a parte: git -C fallirebbe con lo stesso
         * errore di una cartella fuori da ogni repo. */
        if (!sys_is_dir(o.arg[1])) {
            msg_error(MSG_ERR_RELOCATE_NOT_DIR, o.arg[1]);
            goto exit;
        }
        if (repo_locate(o.arg[1], &new_top, &prefix, &objfmt) != GS_OK) {
            msg_error(MSG_ERR_RELOCATE_NOT_REPO, o.arg[1]);
            goto exit;
        }
        rc = relocate_peer(&repo, str_cstr(&old), str_cstr(&new_top));
    } else {
        rc = relocate_self(&repo, str_cstr(&old), (int)o.copy);
    }

exit:
    if (opened != 0) {
        git_batch_close(&batch);
    }
    repo_close(&repo);
    str_free(&objfmt);
    str_free(&prefix);
    str_free(&new_top);
    str_free(&old);
    return rc;
}
