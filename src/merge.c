/*
 * merge.c - unione a 3 vie di un file shared.
 */
#include "merge.h"

#include <stdio.h>
#include <string.h>

void merge_ref(Str_t *ref, const char *id)
{
    str_reset(ref);
    str_addf(ref, "refs/shared-merge/%s", id);
}

int merge_is_live(GitBatch_t *b, const char *path, const Oid_t *h,
                  int *unmerged)
{
    Str_t name = STR_INIT;
    Oid_t oid;
    int live;

    /* ":2:<path>" e ":3:<path>": stage di conflitto (nostra, loro). */
    str_addf(&name, ":2:%s", path);
    *unmerged = git_batch_blob(b, str_cstr(&name), &oid);
    if (*unmerged == 0) {
        str_reset(&name);
        str_addf(&name, ":3:%s", path);
        *unmerged = git_batch_blob(b, str_cstr(&name), &oid);
    }
    live = *unmerged;
    if (live == 0) {
        /* Risoluzione in staging: index diverso da HEAD. Index uguale a
         * HEAD: il merge e' stato abbandonato (git restore). */
        str_reset(&name);
        str_addf(&name, ":0:%s", path);
        live = git_batch_blob(b, str_cstr(&name), &oid) && !oid_eq(&oid, h);
    }
    str_free(&name);
    return live;
}

eGsErr_t merge_empty_blob(Oid_t *out)
{
    static const char *const args[] = { "hash-object", "-w", "-t", "blob",
                                        "--stdin", NULL };
    Str_t hex = STR_INIT;
    Proc_t p;
    int code = 0;
    eGsErr_t err = git_start(&p, args, PROC_STDIN_PIPE);

    if (err == GS_OK) {
        proc_close_in(&p); /* stdin vuoto = blob vuoto */
        err = proc_collect(&p, &hex, &code);
        str_trim_eol(&hex);
        if ((err == GS_OK) &&
            ((code != 0) || !oid_from_hex(out, hex.buf, hex.len))) {
            err = GS_ERR_GIT;
        }
    }
    str_free(&hex);
    return err;
}

eGsErr_t merge_index_blob(const char *top, const char *path, Oid_t *out)
{
    const char *args[] = { "-C", top, "rev-parse", "-q", "--verify", NULL,
                           NULL };
    Str_t name = STR_INIT;
    Str_t hex = STR_INIT;
    eGsErr_t err;

    str_addf(&name, ":0:%s", path);
    args[5] = str_cstr(&name);
    err = git_run(args, PROC_STDERR_NULL, &hex);
    if ((err == GS_OK) && !oid_from_hex(out, hex.buf, hex.len)) {
        err = GS_ERR_FORMAT;
    }
    str_free(&hex);
    str_free(&name);
    return err;
}

/* Stage 1/2/3 del path nell'index, al posto della voce attuale. Record
 * terminati da '\0' (-z): il path arriva cosi' com'e'. */
static eGsErr_t set_stages(const char *top, const char *path,
                           const Oid_t *base, const Oid_t *ours,
                           const Oid_t *theirs)
{
    const char *args[] = { "-C", top, "update-index", "-z", "--index-info",
                           NULL };
    const Oid_t *const stage[] = { base, ours, theirs };
    char hex[OID_HEX_SIZE];
    Str_t out = STR_INIT;
    Proc_t p;
    size_t i;
    int code = 0;
    int put_rc;
    eGsErr_t err = git_start(&p, args, PROC_STDIN_PIPE);

    if (err != GS_OK) {
        goto exit;
    }
    /* Modo 0 con oid nullo: toglie la voce di stage 0. */
    memset(hex, '0', 2U * (size_t)ours->len);
    hex[2U * (size_t)ours->len] = '\0';
    put_rc = fprintf(p.in, "0 %s\t%s%c", hex, path, '\0');
    for (i = 0U; (i < 3U) && (put_rc >= 0); i++) {
        /* Modo sempre 100644 nella v0 (bit di esecuzione: A-15). */
        oid_to_hex(stage[i], hex);
        put_rc = fprintf(p.in, "100644 %s %u\t%s%c", hex, (unsigned)i + 1U,
                         path, '\0');
    }
    proc_close_in(&p);
    err = proc_collect(&p, &out, &code);
    if ((err == GS_OK) && ((put_rc < 0) || (code != 0))) {
        err = GS_ERR_GIT;
    }

exit:
    str_free(&out);
    return err;
}

eGsErr_t merge_three(const char *top, const char *path, const Oid_t *base,
                     const Oid_t *ours, const Oid_t *theirs, int *conflict)
{
    const char *a_merge[] = { "-C", top, "merge-index", "git-merge-one-file",
                              "--", path, NULL };
    const char *a_marks[] = { "-C", top, "--literal-pathspecs", "checkout",
                              "-m", "--", path, NULL };
    Str_t out = STR_INIT;
    int code = 0;
    eGsErr_t err = set_stages(top, path, base, ours, theirs);

    *conflict = 0;
    if (err == GS_OK) {
        /* 0 = unione pulita: stage 0 e file scritti. Altro: gli stage
         * restano (conflitto, file binario, programma assente). */
        err = git_capture(a_merge, PROC_STDERR_NULL, &out, &code);
    }
    if ((err == GS_OK) && (code != 0)) {
        *conflict = 1;
        /* Riscrive il file dagli stage, con le etichette "ours" e
         * "theirs" e lo stile di merge.conflictStyle. Se non riesce
         * restano i marker di merge-one-file: il conflitto c'e' comunque. */
        (void)git_capture(a_marks, PROC_STDERR_NULL, &out, &code);
    }
    str_free(&out);
    return err;
}

eGsErr_t merge_base(const Oid_t *a, const Oid_t *b, Oid_t *out)
{
    const char *args[] = { "merge-base", NULL, NULL, NULL };
    char hex_a[OID_HEX_SIZE];
    char hex_b[OID_HEX_SIZE];
    Str_t hex = STR_INIT;
    int code = 0;
    eGsErr_t err;

    oid_to_hex(a, hex_a);
    oid_to_hex(b, hex_b);
    args[1] = hex_a;
    args[2] = hex_b;
    out->len = 0U;
    err = git_capture(args, PROC_STDERR_NULL, &hex, &code);
    str_trim_eol(&hex);
    if ((err == GS_OK) && (code == 0)) {
        if (!oid_from_hex(out, hex.buf, hex.len)) {
            err = GS_ERR_FORMAT;
        }
    } else if ((err == GS_OK) && (code != 1)) { /* 1 = nessun antenato */
        err = GS_ERR_GIT;
    } else {
        /* errore di avvio gia' in err, oppure nessun antenato comune */
    }
    str_free(&hex);
    return err;
}
