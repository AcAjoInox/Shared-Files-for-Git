/*
 * chain.c - la catena delle versioni.
 */
#include "chain.h"

#include <stdio.h>

eGsErr_t chain_create_first(const char *id, const Oid_t *blob,
                            const char *name)
{
    static const char *const a_mktree[] = { "mktree", NULL };
    const char *a_commit[] = { "commit-tree", NULL, "-m", NULL, NULL };
    const char *a_ref[] = { "update-ref", "-m", "git shared: first version",
                            NULL, NULL, "", NULL };
    char hex[OID_HEX_SIZE];
    Str_t tree = STR_INIT;
    Str_t commit = STR_INIT;
    Str_t msg = STR_INIT;
    Str_t ref = STR_INIT;
    Proc_t p;
    int code = 0;
    eGsErr_t err;

    /* 1. Tree con la sola voce "content" (mktree legge lo stdin). */
    oid_to_hex(blob, hex);
    err = git_start(&p, a_mktree, PROC_STDIN_PIPE);
    if (err != GS_OK) {
        goto exit;
    }
    if (fprintf(p.in, "100644 blob %s\tcontent\n", hex) < 0) {
        err = GS_ERR_GIT;
    }
    proc_close_in(&p);
    if (proc_collect(&p, &tree, &code) != GS_OK) {
        err = GS_ERR_PROC;
    }
    str_trim_eol(&tree);
    if ((err != GS_OK) || (code != 0) || (tree.len == 0U)) {
        err = (err != GS_OK) ? err : GS_ERR_GIT;
        goto exit;
    }

    /* 2. Commit senza genitori: autore e data dell'utente (D-013). */
    str_addf(&msg, "shared: %s", name);
    a_commit[1] = str_cstr(&tree);
    a_commit[3] = str_cstr(&msg);
    err = git_run(a_commit, 0U, &commit);
    if (err != GS_OK) {
        goto exit;
    }

    /* 3. La ref, solo se non esiste: il valore vecchio "" lo pretende. */
    str_addf(&ref, "refs/shared/%s", id);
    a_ref[3] = str_cstr(&ref);
    a_ref[4] = str_cstr(&commit);
    err = git_run(a_ref, 0U, NULL);

exit:
    str_free(&ref);
    str_free(&msg);
    str_free(&commit);
    str_free(&tree);
    return err;
}

eGsErr_t chain_blobs(GitBatch_t *b, const char *id, OidList_t *out)
{
    char hex[OID_HEX_SIZE];
    Str_t name = STR_INIT;
    Oid_t commit;
    Oid_t blob;
    eGitObj_t type;
    eGsErr_t err = GS_OK;

    out->n = 0U;
    str_addf(&name, "refs/shared/%s", id);
    type = git_batch_query(b, str_cstr(&name), &commit);
    while (type == GIT_OBJ_COMMIT) {
        oid_to_hex(&commit, hex);
        str_reset(&name);
        str_addf(&name, "%s:content", hex);
        if (git_batch_blob(b, str_cstr(&name), &blob) == 0) {
            err = GS_ERR_FORMAT; /* versione senza "content" */
            type = GIT_OBJ_MISSING;
        } else {
            oidlist_add(out, &blob);
            str_reset(&name);
            str_addf(&name, "%s^", hex);
            type = git_batch_query(b, str_cstr(&name), &commit);
        }
    }
    if (type == GIT_OBJ_ERROR) {
        err = GS_ERR_PROC;
    }
    str_free(&name);
    return err;
}
