/*
 * chain.c - la catena delle versioni.
 */
#include "chain.h"

#include <stdio.h>

void chain_ref(Str_t *ref, const char *id)
{
    str_reset(ref);
    str_addf(ref, "refs/shared/%s", id);
}

eGsErr_t chain_commit(const Oid_t *blob, const Oid_t *parent,
                      const Oid_t *parent2, const char *name, Oid_t *out)
{
    static const char *const a_mktree[] = { "mktree", NULL };
    const char *a_commit[] = { "commit-tree", NULL, "-m", NULL, NULL, NULL,
                               NULL, NULL, NULL };
    char hex[OID_HEX_SIZE];
    char parent_hex[OID_HEX_SIZE];
    char parent2_hex[OID_HEX_SIZE];
    Str_t tree = STR_INIT;
    Str_t commit = STR_INIT;
    Str_t msg = STR_INIT;
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

    /* 2. Il commit. */
    str_addf(&msg, "shared: %s", name);
    a_commit[1] = str_cstr(&tree);
    a_commit[3] = str_cstr(&msg);
    if (parent != NULL) {
        oid_to_hex(parent, parent_hex);
        a_commit[4] = "-p";
        a_commit[5] = parent_hex;
        if (parent2 != NULL) {
            oid_to_hex(parent2, parent2_hex);
            a_commit[6] = "-p";
            a_commit[7] = parent2_hex;
        }
    }
    err = git_run(a_commit, 0U, &commit);
    if ((err == GS_OK) && !oid_from_hex(out, commit.buf, commit.len)) {
        err = GS_ERR_FORMAT;
    }

exit:
    str_free(&msg);
    str_free(&commit);
    str_free(&tree);
    return err;
}

eGsErr_t chain_create_first(const char *id, const Oid_t *blob,
                            const char *name)
{
    Str_t ref = STR_INIT;
    Oid_t commit;
    eGsErr_t err = chain_commit(blob, NULL, NULL, name, &commit);

    if (err == GS_OK) {
        chain_ref(&ref, id);
        err = git_update_ref(str_cstr(&ref), &commit, NULL,
                             "git shared: first version");
    }
    str_free(&ref);
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
    chain_ref(&name, id);
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
