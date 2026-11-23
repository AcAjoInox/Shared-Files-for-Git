/*
 * cmd_prune.c - "git shared prune [-n] [<path>]" (REQ-F-014, D-082).
 *
 * Per ogni file shared collegato (o quello indicato), ogni peer e':
 *   - membro:          la sua repo ha la sezione [shared "<id>"]: resta;
 *   - uscito:          la repo c'e' ma non condivide piu' il file (es. un
 *                      unlink mentre questa repo era irraggiungibile, B-07);
 *   - sparito:         al suo path non c'e' piu' una repo;
 *   - irraggiungibile: la repo c'e' ma git non riesce a leggerla: resta,
 *                      con un avviso. Si toglie solo cio' che si e' visto
 *                      sparire, mai cio' che adesso non risponde (B-07).
 * Uscito e sparito si tolgono dalla membership locale e da quella dei
 * peer membri. Effetto immediato: sono dati locali, non versionati
 * (REQ-F-023).
 *
 * Senza <path> toglie anche le ref di lavoro rimaste senza sezione locale
 * (refs/shared/<id>, refs/shared-incoming/<id>/<k>, refs/shared-merge/<id>,
 * refs/shared-creator/<id>):
 * reportIdea B-27, C-22. E gli avvisi rimasti senza sezione (D-099).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmd.h"
#include "cmd_common.h"
#include "inbox.h"
#include "mem.h"
#include "peers.h"

/* Nome da mostrare: il path, o l'ID se qui .gitshared non lo dichiara. */
static const char *label_of(const SharedEntry_t *e)
{
    return e->in_gitshared ? str_cstr(&e->path) : e->id;
}

/* Toglie il peer dalla membership locale. */
static eGsErr_t forget_local(const char *id, const char *peer)
{
    const char *args[] = { "config", "--fixed-value", "--unset-all", NULL,
                           peer, NULL };
    Str_t key = STR_INIT;
    Str_t out = STR_INIT;
    int code = 0;
    eGsErr_t err;

    str_addf(&key, "shared.%s.peer", id);
    args[3] = str_cstr(&key);
    err = git_capture(args, PROC_STDERR_NULL, &out, &code);
    if ((err == GS_OK) && (code != 0) && (code != 5)) { /* 5 = non c'era */
        err = GS_ERR_GIT;
    }
    str_free(&out);
    str_free(&key);
    return err;
}

/* I peer di un file. Ritorna il numero di peer tolti (o da togliere). */
static unsigned prune_entry(const Repo_t *r, const SharedEntry_t *e, int dry,
                            int *rc)
{
    static const eMsgId_t done[] = {
        [PEER_LEFT] = MSG_PRUNE_LEFT, [PEER_GONE] = MSG_PRUNE_GONE,
    };
    static const eMsgId_t would[] = {
        [PEER_LEFT] = MSG_PRUNE_WOULD_LEFT, [PEER_GONE] = MSG_PRUNE_WOULD_GONE,
    };
    PeerTally_t tally = { 0U, 0U, 0U };
    const char *label = label_of(e);
    size_t n = str_zcount(&e->peers);
    uint8_t *state = xmalloc((n > 0U) ? n : 1U);
    Str_t members = STR_INIT;
    Str_t removed = STR_INIT;
    const char *p;
    size_t k;
    unsigned count = 0U;

    peers_check(e->id, &e->peers, state);
    for (k = 0U, p = str_znext(&e->peers, NULL); p != NULL;
         k++, p = str_znext(&e->peers, p)) {
        if (str_eq(&r->top, p)) {
            continue; /* se stessa: non dovrebbe esserci, non si tocca */
        }
        if (state[k] == PEER_MEMBER) {
            str_addz(&members, p);
        } else if (state[k] == PEER_UNREACHABLE) {
            msg_warning(MSG_WARN_PRUNE_KEPT, label, p);
        } else if (dry) {
            printf(msg(would[state[k]]), label, p);
            putchar('\n');
            count++;
        } else if (forget_local(e->id, p) != GS_OK) {
            msg_error(MSG_ERR_PRUNE, label, p);
            *rc = GS_EXIT_ERROR;
        } else {
            printf(msg(done[state[k]]), label, p);
            putchar('\n');
            str_addz(&removed, p);
            count++;
        }
    }
    /* Anche i peer membri dimenticano i peer tolti (REQ-F-014). */
    for (p = str_znext(&removed, NULL); (p != NULL) && (members.len > 0U);
         p = str_znext(&removed, p)) {
        peers_forget(label, e->id, &members, p, &tally);
    }
    str_free(&removed);
    str_free(&members);
    free(state);
    return count;
}

/* ID di una ref di lavoro: refs/shared/<id>, refs/shared-merge/<id>,
 * refs/shared-incoming/<id>/<k>, refs/shared-creator/<id>. 0 se il nome
 * non ha questa forma. */
static int ref_id(const char *ref, char id[GS_ID_HEX_LEN + 1U])
{
    static const char *const prefix[] = { "refs/shared/", "refs/shared-merge/",
                                          "refs/shared-incoming/",
                                          "refs/shared-creator/" };
    const char *rest = NULL;
    size_t i;
    size_t len;

    for (i = 0U; (i < (sizeof prefix / sizeof prefix[0])) && (rest == NULL); i++) {
        len = strlen(prefix[i]);
        if (strncmp(ref, prefix[i], len) == 0) {
            rest = ref + len;
        }
    }
    if ((rest == NULL) || (strlen(rest) < GS_ID_HEX_LEN) ||
        ((rest[GS_ID_HEX_LEN] != '\0') && (rest[GS_ID_HEX_LEN] != '/'))) {
        return 0;
    }
    memcpy(id, rest, GS_ID_HEX_LEN);
    id[GS_ID_HEX_LEN] = '\0';
    return 1;
}

/* Ref di lavoro senza sezione locale. Ritorna quante (da) togliere. */
static unsigned prune_refs(Repo_t *r, int dry, int *rc)
{
    static const char *const args[] = { "for-each-ref", "--format=%(refname)",
                                        "refs/shared", "refs/shared-merge",
                                        "refs/shared-incoming",
                                        "refs/shared-creator", NULL };
    const SharedEntry_t *e;
    Str_t lines = STR_INIT;
    Str_t orphans = STR_INIT;
    char id[GS_ID_HEX_LEN + 1U];
    char *line;
    char *nl;
    const char *o;
    unsigned count = 0U;

    if (git_run(args, 0U, &lines) != GS_OK) {
        msg_fatal(MSG_ERR_PROC, "git for-each-ref");
        *rc = GS_EXIT_FATAL;
        goto exit;
    }
    for (line = lines.buf; (line != NULL) && (*line != '\0');
         line = (nl != NULL) ? (nl + 1) : NULL) {
        nl = strchr(line, '\n');
        if (nl != NULL) {
            *nl = '\0';
        }
        if (ref_id(line, id)) {
            e = repo_find_id(r, id);
            if ((e == NULL) || !e->in_config) {
                str_addz(&orphans, line);
            }
        }
    }
    for (o = str_znext(&orphans, NULL); o != NULL; o = str_znext(&orphans, o)) {
        printf(msg(dry ? MSG_PRUNE_WOULD_REF : MSG_PRUNE_REF), o);
        putchar('\n');
        count++;
    }
    if (!dry && (orphans.len > 0U) && (git_delete_refs(&orphans) != GS_OK)) {
        msg_error(MSG_ERR_CHAIN, "refs/shared");
        *rc = GS_EXIT_ERROR;
    }

exit:
    str_free(&orphans);
    str_free(&lines);
    return count;
}

/* Avvisi di ID senza sezione locale (D-099), per esempio dopo un unlink
 * con un peer che ancora non lo sapeva. Ritorna quanti (da) togliere. */
static unsigned prune_inbox(Repo_t *r, int dry)
{
    const SharedEntry_t *e;
    Str_t ids = STR_INIT;
    Str_t shown = STR_INIT;
    const char *id;
    unsigned count = 0U;

    inbox_ids(str_cstr(&r->top), &ids);
    for (id = str_znext(&ids, NULL); id != NULL; id = str_znext(&ids, id)) {
        e = repo_find_id(r, id);
        if ((e == NULL) || !e->in_config) {
            str_reset(&shown);
            str_addf(&shown, ".git/shared/inbox/%s", id);
            printf(msg(dry ? MSG_PRUNE_WOULD_REF : MSG_PRUNE_REF), str_cstr(&shown));
            putchar('\n');
            if (!dry) {
                inbox_remove(str_cstr(&r->top), id);
            }
            count++;
        }
    }
    str_free(&shown);
    str_free(&ids);
    return count;
}

int cmd_prune(int argc, char **argv)
{
    CmdOpts_t o;
    Repo_t repo = REPO_INIT;
    GitBatch_t batch;
    const SharedEntry_t *e;
    Str_t full = STR_INIT;
    const char *path = NULL;
    unsigned count = 0U;
    size_t i;
    int first;
    int opened = 0;
    int rc = GS_EXIT_USAGE;

    first = cmd_parse(argc, argv, CMD_OPT_DRY_RUN, MSG_USAGE_PRUNE, &o);
    if (first < 0) {
        goto exit;
    }
    if ((argc - first) > 1) {
        msg_error(MSG_ERR_TOO_MANY_ARGS);
        fputs(msg(MSG_USAGE_PRUNE), stderr);
        goto exit;
    }
    rc = cmd_open(&repo, &batch);
    if (rc != GS_EXIT_OK) {
        goto exit;
    }
    opened = 1;

    if (first < argc) {
        e = cmd_repo_path(&repo, argv[first], &full)
                ? repo_find_path(&repo, str_cstr(&full))
                : NULL;
        if ((e == NULL) || !e->in_config) {
            msg_error(MSG_ERR_NOT_SHARED_PATH, argv[first]);
            rc = GS_EXIT_ERROR;
            goto exit;
        }
        path = str_cstr(&full);
    }

    /* Collegati, anche se qui .gitshared non li dichiara (la membership e'
     * per repo); non un set o un link ancora in attesa. */
    for (i = 0U; i < repo.n; i++) {
        e = &repo.v[i];
        if (e->in_config && (e->pending != SHARED_PENDING_SET) &&
            (e->pending != SHARED_PENDING_LINK) &&
            ((path == NULL) || (e->in_gitshared && str_eq(&e->path, path)))) {
            count += prune_entry(&repo, e, (int)o.dry_run, &rc);
        }
    }
    if (path == NULL) {
        count += prune_refs(&repo, (int)o.dry_run, &rc);
        count += prune_inbox(&repo, (int)o.dry_run);
    }
    if ((count == 0U) && (rc == GS_EXIT_OK)) {
        puts(msg(MSG_PRUNE_NOTHING));
    }

exit:
    if (opened != 0) {
        git_batch_close(&batch);
        repo_close(&repo);
    }
    str_free(&full);
    return rc;
}
