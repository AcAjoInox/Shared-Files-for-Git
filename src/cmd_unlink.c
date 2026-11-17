/*
 * cmd_unlink.c - "git shared unlink [-c | -m <msg>] [--] <path>"
 * (REQ-F-003, REQ-F-023, D-081).
 *
 * Al comando: sezione tolta da .gitshared (il file .gitshared sparisce se
 * resta vuoto), .gitshared in staging, attesa "unlink" in .git/config con
 * il branch e il commit del comando. Il file non si tocca: resta un file
 * normale. Al commit (repo_complete_pending): i peer dimenticano questa
 * repo, refs/shared/<id> e la sezione locale spariscono. Con -c/-m il
 * commit si fa subito. git restore .gitshared annulla.
 *
 * Due casi senza attesa:
 *   - file scollegato (es. un clone): non ci sono dati locali, si toglie
 *     solo la sezione da .gitshared;
 *   - <path> e' un ID di una sezione locale senza dichiarazione in nessun
 *     branch (reportIdea B-45): i dati locali si tolgono subito.
 */
#include <stdio.h>
#include <string.h>

#include "cmd.h"
#include "cmd_common.h"
#include "sys.h"

/* Toglie la sezione da .gitshared; se non resta nessuna sezione, toglie
 * il file. Poi lo mette in staging (anche la cancellazione). */
static eGsErr_t drop_section(const Repo_t *r, const char *name)
{
    const char *args[] = { "config", "-f", NULL, "--remove-section", NULL,
                           NULL };
    Repo_t left = REPO_INIT;
    Str_t file = STR_INIT;
    Str_t section = STR_INIT;
    eGsErr_t err;

    str_addf(&file, "%s/.gitshared", str_cstr(&r->top));
    str_addf(&section, "shared.%s", name);
    args[2] = str_cstr(&file);
    args[4] = str_cstr(&section);
    err = git_run(args, 0U, NULL);
    if (err == GS_OK) {
        err = repo_load_gitshared(&left, str_cstr(&file), 0, 0);
    }
    if ((err == GS_OK) && (left.n == 0U) && (sys_remove(str_cstr(&file)) != 0)) {
        err = GS_ERR_SYS;
    }
    if (err == GS_OK) {
        err = cmd_stage(r, ".gitshared");
    }
    repo_close(&left);
    str_free(&section);
    str_free(&file);
    return err;
}

/* <arg> e' l'ID di una sezione locale che nessun .gitshared dichiara:
 * dati locali tolti subito. Ritorna il codice di uscita. */
static int unlink_orphan(Repo_t *r, GitBatch_t *b, SharedEntry_t *e)
{
    Str_t branch = STR_INIT;
    int found = 0;
    int rc = GS_EXIT_ERROR;

    if (repo_declaring_branch(e->id, b, &branch, NULL, &found) != GS_OK) {
        msg_fatal(MSG_ERR_PROC, "git");
        rc = GS_EXIT_FATAL;
    } else if (found != 0) {
        /* es. un branch nato prima del set (B-16): il file e' shared */
        msg_error(MSG_ERR_UNLINK_ELSEWHERE, e->id, str_cstr(&branch));
    } else if (repo_forget(r, e, b) != GS_OK) {
        msg_error(MSG_ERR_PENDING);
    } else {
        printf(msg(MSG_UNLINK_ORPHAN), e->id);
        putchar('\n');
        rc = GS_EXIT_OK;
    }
    str_free(&branch);
    return rc;
}

int cmd_unlink(int argc, char **argv)
{
    CmdOpts_t o;
    Repo_t repo = REPO_INIT;
    GitBatch_t batch;
    SharedEntry_t *e = NULL;
    Str_t full = STR_INIT;
    Str_t name = STR_INIT;
    Str_t text = STR_INIT;
    Str_t paths = STR_INIT;
    char id[GS_ID_HEX_LEN + 1U];
    char head_hex[OID_HEX_SIZE];
    Oid_t head;
    int linked;
    int declared = 0;
    int first;
    int opened = 0;
    int rc = GS_EXIT_USAGE;

    first = cmd_parse(argc, argv, CMD_OPT_COMMIT, MSG_USAGE_UNLINK, &o);
    if (first < 0) {
        goto exit;
    }
    if ((first + 1) != argc) {
        msg_error((first >= argc) ? MSG_ERR_MISSING_PATH : MSG_ERR_TOO_MANY_ARGS);
        fputs(msg(MSG_USAGE_UNLINK), stderr);
        goto exit;
    }
    rc = cmd_open(&repo, &batch);
    if (rc != GS_EXIT_OK) {
        goto exit;
    }
    opened = 1;
    rc = GS_EXIT_ERROR;

    /* --- quale file -------------------------------------------------- */
    if (cmd_repo_path(&repo, argv[first], &full)) {
        e = repo_find_path(&repo, str_cstr(&full));
    }
    if (e == NULL) {
        e = repo_find_id(&repo, argv[first]);
        if ((e != NULL) && e->in_config && !e->in_gitshared &&
            (e->pending == SHARED_PENDING_NONE)) {
            rc = unlink_orphan(&repo, &batch, e);
        } else {
            msg_error(MSG_ERR_NOT_SHARED_PATH, argv[first]);
        }
        goto exit;
    }
    if (!e->valid) {
        msg_error(MSG_ERR_NOT_SHARED_PATH, argv[first]);
        goto exit;
    }
    if (e->pending != SHARED_PENDING_NONE) {
        msg_error(MSG_ERR_PENDING_OP, str_cstr(&e->path));
        goto exit;
    }
    str_add(&name, str_cstr(&e->name));
    memcpy(id, e->id, sizeof id);
    linked = e->in_config;

    /* --- attesa: per prima, con il commit del comando ----------------- */
    if (linked) {
        /* Il commit di conferma sara' un discendente di HEAD che non
         * dichiara piu' l'ID: HEAD deve dichiararlo. */
        if ((git_batch_query(&batch, "HEAD", &head) != GIT_OBJ_COMMIT) ||
            (repo_rev_declares(&batch, "HEAD", id, &declared) != GS_OK) ||
            (declared == 0)) {
            msg_error(MSG_ERR_UNLINK_UNCOMMITTED, str_cstr(&full));
            goto exit;
        }
        oid_to_hex(&head, head_hex);
        if (cmd_set_pending(&repo, id, "unlink", str_cstr(&full), NULL,
                            head_hex) != GS_OK) {
            msg_error(MSG_ERR_PENDING);
            goto exit;
        }
    }

    /* --- .gitshared ---------------------------------------------------- */
    if (drop_section(&repo, str_cstr(&name)) != GS_OK) {
        msg_error(MSG_ERR_WRITE_GITSHARED);
        goto exit;
    }
    e->in_gitshared = 0U;
    if (linked) {
        str_add(&e->pending_path, str_cstr(&full));
        str_add(&e->pending_branch, str_cstr(&repo.branch));
        str_add(&e->pending_commit, head_hex);
        e->pending = SHARED_PENDING_UNLINK;
    }

    if (o.commit == 0U) {
        printf(msg(MSG_UNLINK_STAGED), str_cstr(&full));
        putchar('\n');
        rc = GS_EXIT_OK;
        goto exit;
    }

    /* --- commit dedicato del solo .gitshared (D-075) -------------------- */
    if (o.message == NULL) {
        str_addf(&text, msg(MSG_UNLINK_COMMIT_MSG), str_cstr(&full));
        o.message = str_cstr(&text);
    }
    str_addz(&paths, ".gitshared");
    if (cmd_commit(&repo, o.message, &paths) != GS_OK) {
        msg_error(MSG_ERR_COMMIT);
        goto exit;
    }
    if (cmd_reopen(&repo, &batch) != GS_EXIT_OK) {
        opened = 0;
        goto exit;
    }
    e = repo_find_id(&repo, id);
    if ((e == NULL) || !e->in_config) {
        printf(msg(MSG_UNLINK_DONE), str_cstr(&full));
        putchar('\n');
    }
    rc = GS_EXIT_OK;

exit:
    if (opened != 0) {
        git_batch_close(&batch);
        repo_close(&repo);
    }
    str_free(&paths);
    str_free(&text);
    str_free(&name);
    str_free(&full);
    return rc;
}
