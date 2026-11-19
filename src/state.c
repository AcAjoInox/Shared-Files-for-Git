/*
 * state.c - derivazione dello stato e della base.
 *
 * La base B e' la versione da cui parte il contenuto del branch. Si ricava
 * dalla storia del file letta dalla piu' vecchia alla piu' recente: a ogni
 * blob che e' una versione, B diventa la versione piu' vecchia con quel
 * blob che non sia piu' vecchia di B. Un blob presente solo in versioni piu'
 * vecchie di B e' una regressione (es. un revert): B non cambia.
 *
 *   catena v1(A) v2(B);     storia A, B, A  -> B = v2 = T: da pubblicare
 *   catena v1(A) v2(B);     storia A        -> B = v1, H = A: da applicare
 *   catena v1(A) v2(B) v3(A); storia A, B, D -> B = v2, H = D: divergente
 *
 * La catena comprende entrambi i genitori delle unioni (chain_blobs,
 * reportIdea B-46): la versione di un peer unita da un'altra repo e' una
 * versione anche per quel peer.
 *
 *   catena v1(A) vP(P) vQ(Q) M(PQ) V2(Z), M con genitori vP e vQ;
 *   storia del peer Q: A, Q -> B = vQ, H = Q: da applicare
 */
#include "state.h"

#include <stdlib.h>
#include <string.h>

#include "chain.h"
#include "mem.h"
#include "merge.h"
#include "msg.h"

int state_is_derived(eState_t st)
{
    return (st == STATE_ALIGNED) || (st == STATE_TO_APPLY) ||
           (st == STATE_TO_PUBLISH) || (st == STATE_DIVERGED) ||
           (st == STATE_TO_COMMIT) || (st == STATE_MERGING);
}

const char *state_name(eState_t st)
{
    static const eMsgId_t names[] = {
        [STATE_ALIGNED] = MSG_STATE_ALIGNED,
        [STATE_TO_APPLY] = MSG_STATE_TO_APPLY,
        [STATE_TO_PUBLISH] = MSG_STATE_TO_PUBLISH,
        [STATE_DIVERGED] = MSG_STATE_DIVERGED,
        [STATE_TO_COMMIT] = MSG_STATE_TO_COMMIT,
        [STATE_MERGING] = MSG_STATE_MERGING,
        [STATE_PENDING] = MSG_STATE_PENDING,
        [STATE_UNLINKED] = MSG_STATE_UNLINKED,
        [STATE_INCONSISTENT] = MSG_STATE_INCONSISTENT,
        [STATE_ABSENT] = MSG_STATE_ABSENT,
    };

    return msg(names[st]);
}

/* Nuovo blob di una riga di `git log --raw`:
 * ":<modo> <modo> <oid vecchio> <oid nuovo> <stato>\t<path>". */
static int raw_new_blob(const char *line, Oid_t *oid)
{
    const char *f = line;
    const char *sp;
    unsigned i;
    int ok = (line[0] == ':');

    for (i = 0U; ok && (i < 3U); i++) {
        f = strchr(f, ' ');
        ok = (f != NULL);
        if (ok) {
            f++;
        }
    }
    if (ok) {
        sp = strchr(f, ' ');
        ok = (sp != NULL) && oid_from_hex(oid, f, (size_t)(sp - f));
    }
    return ok;
}

/*
 * Base nella catena (chain->v[0] = T, indici crescenti = piu' vecchie).
 * *found = 0 se nessuna versione compare nella storia del file.
 *
 * Un solo processo git log. Se la versione piu' recente che compare e' T,
 * la base e' T comunque: ci si ferma subito senza leggere il resto.
 *
 * La storia e' quella del path e dei path di prima delle rinomine fatte
 * con git shared mv (old_paths, D-101): nel commit della rinomina il file
 * nasce al path nuovo, e da solo la storia comincerebbe li'. I path sono
 * scritti in .gitshared, non indovinati per somiglianza come fa --follow.
 */
eGsErr_t state_find_base(const Repo_t *r, const char *path,
                         const Str_t *old_paths, const OidList_t *chain,
                         size_t *base, int *found)
{
    /* git log e' porcelain: ogni opzione che la configurazione dell'utente
     * puo' cambiare e' fissata qui (reportIdea B-32, D-094). --root contro
     * log.showRoot=false, --no-follow contro log.follow=true (seguirebbe le
     * rinomine anche con --no-renames), --diff-merges=off contro
     * log.diffMerges, --no-show-signature contro log.showSignature (un gpg
     * per commit). Provato con git 2.34 e 2.43. */
    static const char *const fixed[] = { "--literal-pathspecs", "-C", NULL,
                                         "log", "--format=", "--raw",
                                         "--no-abbrev", "--no-renames",
                                         "--root", "--no-follow",
                                         "--diff-merges=off",
                                         "--no-show-signature", "--" };
    const size_t nfixed = sizeof fixed / sizeof fixed[0];
    size_t nold = (old_paths != NULL) ? str_zcount(old_paths) : 0U;
    const char **args = xmalloc((nfixed + nold + 2U) * sizeof *args);
    const char *op;
    OidList_t occ = OIDLIST_INIT; /* blob-versione nella storia, dal recente */
    Str_t line = STR_INIT;
    Oid_t blob;
    Proc_t p;
    size_t j;
    size_t k;
    size_t seen = 0U;
    int have = 0;
    int t_first = 0;
    int hit;
    int code = 0;
    eGsErr_t err;

    for (j = 0U; j < nfixed; j++) {
        args[j] = fixed[j];
    }
    args[2] = str_cstr(&r->top);
    args[j++] = path; /* dopo "--" */
    for (op = (nold > 0U) ? str_znext(old_paths, NULL) : NULL; op != NULL;
         op = str_znext(old_paths, op)) {
        args[j++] = op;
    }
    args[j] = NULL;

    *found = 0;
    /* stderr scartato: chiudendo presto, git puo' lamentare la pipe chiusa */
    err = git_start(&p, args, PROC_STDERR_NULL);
    if (err != GS_OK) {
        goto exit;
    }
    while ((t_first == 0) && (proc_read(&p, '\n', &line) != 0)) {
        if (raw_new_blob(str_cstr(&line), &blob) && oidlist_has(chain, &blob)) {
            if ((occ.n == 0U) && oid_eq(&blob, &chain->v[0])) {
                t_first = 1; /* T e' la piu' recente: base = T */
            } else {
                oidlist_add(&occ, &blob);
            }
        }
    }
    (void)proc_finish(&p, &code); /* il codice non conta se si e' chiuso presto */

    if (t_first != 0) {
        have = 1;
    } else {
        /* Dalla piu' vecchia alla piu' recente (vedi intestazione). */
        for (j = occ.n; j > 0U; j--) {
            hit = 0;
            k = chain->n;
            while ((k > 0U) && (hit == 0)) {
                k--; /* versioni dalla piu' vecchia */
                hit = ((have == 0) || (k <= seen)) &&
                      oid_eq(&chain->v[k], &occ.v[j - 1U]);
            }
            if (hit != 0) {
                seen = k;
                have = 1;
            }
        }
    }
    *base = seen;
    *found = have;

exit:
    free((void *)args);
    oidlist_free(&occ);
    str_free(&line);
    return err;
}

/*
 * Sezione locale senza dichiarazione nel .gitshared del working tree
 * (reportIdea B-16, D-095). Se HEAD la dichiara, .gitshared e' stato
 * toccato a mano: incoerente. Se no, e un altro branch locale la dichiara,
 * questo branch e' nato prima del set (o il set non vi e' mai arrivato):
 * qui il file non e' shared, ed e' come sospeso. Se nessun branch la
 * dichiara la sezione e' orfana: incoerente, si toglie con unlink <id>.
 * Costa un for-each-ref, solo in questo caso raro.
 */
static eGsErr_t undeclared_state(GitBatch_t *b, const char *id, eState_t *st)
{
    Str_t branch = STR_INIT;
    int declared = 0;
    int found = 0;
    eGsErr_t err = repo_rev_declares(b, "HEAD", id, &declared);

    *st = STATE_INCONSISTENT;
    if ((err == GS_OK) && (declared == 0)) {
        err = repo_declaring_branch(id, b, &branch, NULL, &found);
        if ((err == GS_OK) && (found != 0)) {
            *st = STATE_ABSENT;
        }
    }
    str_free(&branch);
    return err;
}

eGsErr_t state_renamed(GitBatch_t *b, const SharedEntry_t *e, Str_t *from,
                       int *found)
{
    Str_t path = STR_INIT;
    Str_t name = STR_INIT;
    Oid_t oid;
    int declared = 0;
    eGsErr_t err = repo_rev_path(b, "HEAD", e->id, &path, &declared);

    *found = 0;
    if ((err == GS_OK) && (declared != 0) && !str_eq(&path, str_cstr(&e->path))) {
        str_addf(&name, "HEAD:%s", str_cstr(&path));
        *found = git_batch_blob(b, str_cstr(&name), &oid);
    }
    if ((*found != 0) && (from != NULL)) {
        str_reset(from);
        str_add(from, str_cstr(&path));
    }
    str_free(&name);
    str_free(&path);
    return err;
}

eGsErr_t state_derive(const Repo_t *r, const SharedEntry_t *e, GitBatch_t *b,
                      eState_t *st, StateInfo_t *info)
{
    OidList_t chain = OIDLIST_INIT;
    Str_t name = STR_INIT;
    char hex[OID_HEX_SIZE];
    StateInfo_t si;
    Oid_t i;
    Oid_t k;
    size_t base = 0U;
    int found = 0;
    int unmerged = 0;
    eGsErr_t err = GS_OK;

    si.h.len = 0U;
    si.t.len = 0U;
    si.tip.len = 0U;
    si.base.len = 0U;

    if (e->pending != SHARED_PENDING_NONE) {
        *st = STATE_PENDING;
        goto exit;
    }
    if (!e->in_config) {
        *st = STATE_UNLINKED;
        goto exit;
    }
    *st = STATE_INCONSISTENT;
    if (!e->in_gitshared) {
        err = undeclared_state(b, e->id, st);
        goto exit;
    }
    str_addf(&name, "HEAD:%s", str_cstr(&e->path));
    if (git_batch_blob(b, str_cstr(&name), &si.h) == 0) {
        /* Rinomina di git shared mv non ancora committata: in attesa di
         * commit (D-101). Altrimenti file cancellato o rinominato senza
         * git shared mv: incoerente (REQ-F-020). */
        si.h.len = 0U;
        err = state_renamed(b, e, NULL, &found);
        if ((err == GS_OK) && (found != 0)) {
            *st = STATE_PENDING;
        }
        goto exit;
    }
    /* Prima il commit, poi il suo blob: T e il suo contenuto sono della
     * stessa versione anche se un peer invia mentre si legge (B-31). */
    chain_ref(&name, e->id);
    if (git_batch_query(b, str_cstr(&name), &si.tip) != GIT_OBJ_COMMIT) {
        si.tip.len = 0U;
        goto exit; /* catena assente */
    }
    oid_to_hex(&si.tip, hex);
    str_reset(&name);
    str_addf(&name, "%s:content", hex);
    if (git_batch_blob(b, str_cstr(&name), &si.t) == 0) {
        goto exit;
    }

    /* Unione in corso (D-078): conta solo se l'index la conferma. Scaduta
     * (abbandonata con git restore, o risoluzione gia' committata) si toglie
     * subito, da qualunque comando la veda: una modifica messa in staging
     * dopo non diventa la sua risoluzione (reportIdea B-48, D-092). Un
     * errore nel toglierla non cambia lo stato: ci riprova il prossimo. */
    merge_ref(&name, e->id);
    if (git_batch_query(b, str_cstr(&name), &k) == GIT_OBJ_COMMIT) {
        if (merge_is_live(b, str_cstr(&e->path), &si.h, &unmerged)) {
            *st = STATE_MERGING;
            goto exit;
        }
        (void)merge_drop(e->id);
    }

    /* Caso comune: un confronto di oid, nessuna storia da leggere. */
    if (oid_eq(&si.h, &si.t)) {
        *st = STATE_ALIGNED;
        goto exit;
    }
    /* L'ultima versione e' gia' nell'index (update senza commit): resta
     * solo il commit (D-076). Una domanda in piu', solo se H != T. */
    str_reset(&name);
    str_addf(&name, ":%s", str_cstr(&e->path));
    if ((git_batch_blob(b, str_cstr(&name), &i) != 0) && oid_eq(&i, &si.t)) {
        *st = STATE_TO_COMMIT;
        goto exit;
    }

    err = chain_blobs(b, e->id, &chain);
    if ((err != GS_OK) || (chain.n == 0U)) {
        goto exit;
    }
    err = state_find_base(r, str_cstr(&e->path), &e->old_paths, &chain, &base,
                          &found);
    if (err != GS_OK) {
        goto exit;
    }
    if (found == 0) {
        *st = STATE_DIVERGED; /* nessuna base (caso D-029) */
    } else if (base == 0U) {
        *st = STATE_TO_PUBLISH;
    } else if (oid_eq(&si.h, &chain.v[base])) {
        *st = STATE_TO_APPLY;
    } else {
        *st = STATE_DIVERGED;
        si.base = chain.v[base];
    }

exit:
    if (info != NULL) {
        *info = si;
    }
    oidlist_free(&chain);
    str_free(&name);
    return err;
}
