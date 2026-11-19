/*
 * chain.c - la catena delle versioni.
 */
#include "chain.h"

#include <stdio.h>
#include <stdlib.h>

#include "mem.h"

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

/* Voce della pila della visita in profondita' di chain_blobs. */
typedef struct {
    Oid_t commit;
    Oid_t blob;   /* valido se done */
    int done;     /* genitori gia' messi in pila: alla prossima uscita e'
                   * finita */
} ChainFrame_t;

typedef struct {
    ChainFrame_t *v;
    size_t n;
    size_t cap;
} ChainStack_t;

static void stack_push(ChainStack_t *s, const Oid_t *commit,
                       const Oid_t *blob, int done)
{
    if (s->n == s->cap) {
        s->cap = (s->cap == 0U) ? 16U : (s->cap * 2U);
        s->v = xrealloc(s->v, s->cap * sizeof *s->v);
    }
    s->v[s->n].commit = *commit;
    s->v[s->n].blob = (blob != NULL) ? *blob : *commit;
    s->v[s->n].done = done;
    s->n++;
}

/* Mette in pila il genitore n-esimo di hex, se c'e' e non e' gia' visitato. */
static eGsErr_t push_parent(GitBatch_t *b, ChainStack_t *s, const char *hex,
                            unsigned n, const OidList_t *seen)
{
    Str_t name = STR_INIT;
    Oid_t parent;
    eGitObj_t type;
    eGsErr_t err = GS_OK;

    str_addf(&name, "%s^%u", hex, n);
    type = git_batch_query(b, str_cstr(&name), &parent);
    if (type == GIT_OBJ_ERROR) {
        err = GS_ERR_PROC;
    } else if ((type == GIT_OBJ_COMMIT) && !oidlist_has(seen, &parent)) {
        stack_push(s, &parent, NULL, 0);
    } else {
        /* nessun genitore n-esimo, o gia' visitato */
    }
    str_free(&name);
    return err;
}

/*
 * Visita in profondita' del grafo delle versioni (reportIdea B-46): ogni
 * versione esce dopo tutti i suoi genitori (post-ordine), quindi l'ordine
 * rovesciato mette ogni versione prima dei suoi genitori. Si mette in pila
 * prima il primo genitore, poi il secondo: il ramo del secondo genitore si
 * visita e finisce per primo, e la linea dei primi genitori resta in testa,
 * contigua, come nella lettura per primo genitore. Una versione ha al piu'
 * due genitori (chain_commit).
 */
eGsErr_t chain_blobs(GitBatch_t *b, const char *id, OidList_t *out)
{
    Str_t ref = STR_INIT;
    eGsErr_t err;

    chain_ref(&ref, id);
    err = chain_blobs_ref(b, str_cstr(&ref), out);
    str_free(&ref);
    return err;
}

eGsErr_t chain_blobs_ref(GitBatch_t *b, const char *ref, OidList_t *out)
{
    char hex[OID_HEX_SIZE];
    ChainStack_t stack = { NULL, 0U, 0U };
    OidList_t seen = OIDLIST_INIT; /* versioni gia' espanse */
    OidList_t post = OIDLIST_INIT; /* blob in post-ordine: dalla piu' vecchia */
    ChainFrame_t f;
    Str_t name = STR_INIT;
    Oid_t commit;
    Oid_t blob;
    eGitObj_t type;
    size_t j;
    eGsErr_t err = GS_OK;

    out->n = 0U;
    type = git_batch_query(b, ref, &commit);
    if (type == GIT_OBJ_COMMIT) {
        stack_push(&stack, &commit, NULL, 0);
    } else if (type == GIT_OBJ_ERROR) {
        err = GS_ERR_PROC;
    } else {
        /* catena assente: lista vuota */
    }

    while ((err == GS_OK) && (stack.n > 0U)) {
        stack.n--;
        f = stack.v[stack.n];
        if (f.done != 0) {
            oidlist_add(&post, &f.blob);
        } else if (!oidlist_has(&seen, &f.commit)) {
            oidlist_add(&seen, &f.commit);
            oid_to_hex(&f.commit, hex);
            str_reset(&name);
            str_addf(&name, "%s:content", hex);
            if (git_batch_blob(b, str_cstr(&name), &blob) == 0) {
                err = GS_ERR_FORMAT; /* versione senza "content" */
            } else {
                stack_push(&stack, &f.commit, &blob, 1);
                err = push_parent(b, &stack, hex, 1U, &seen);
                if (err == GS_OK) {
                    err = push_parent(b, &stack, hex, 2U, &seen);
                }
            }
        } else {
            /* gia' raggiunta da un altro figlio */
        }
    }

    if (err == GS_OK) {
        for (j = post.n; j > 0U; j--) {
            oidlist_add(out, &post.v[j - 1U]);
        }
    }
    free(stack.v);
    oidlist_free(&seen);
    oidlist_free(&post);
    str_free(&name);
    return err;
}
