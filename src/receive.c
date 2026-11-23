/*
 * receive.c - ricevere le versioni dei peer (D-015, D-092, D-099).
 */
#include "receive.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "chain.h"
#include "inbox.h"
#include "mem.h"
#include "merge.h"
#include "peers.h"
#include "remote.h"

void received_free(Received_t *rv)
{
    oidlist_free(&rv->heads);
    str_free(&rv->missing);
    str_free(&rv->to_merge);
    str_free(&rv->creator);
}

static void add_once(Str_t *list, const char *s)
{
    if (!str_zhas(list, s)) {
        str_addz(list, s);
    }
}

/* Oid di un commit presente nella repo: 1 se c'e'. */
static int have_commit(GitBatch_t *b, const Oid_t *oid)
{
    char hex[OID_HEX_SIZE];
    Oid_t got;

    oid_to_hex(oid, hex);
    return git_batch_query(b, hex, &got) == GIT_OBJ_COMMIT;
}

/* Versioni ottenute dal fetch, con la repo da cui vengono. */
typedef struct {
    OidList_t oids;
    Str_t src; /* elenco compatto, stesso ordine */
} Tips_t;

/*
 * Fetch dalle repo dell'elenco in refs/shared-incoming/<id>/<k>; le
 * versioni ottenute vanno in cand e in tips, le ref temporanee in fetched.
 */
static void fetch_from(const SharedEntry_t *e, GitBatch_t *b, const Str_t *from,
                       const Oid_t *t, OidList_t *cand, Tips_t *tips,
                       Str_t *fetched)
{
    size_t n = str_zcount(from);
    uint8_t *ok = xmalloc((n > 0U) ? n : 1U);
    PeerTally_t tally = { 0U, 0U, 0U };
    Str_t incoming = STR_INIT;
    const char *src;
    Oid_t in;
    size_t k;

    peers_fetch(str_cstr(&e->path), e->id, from, ok, &tally);
    for (k = 0U, src = str_znext(from, NULL); k < n;
         k++, src = str_znext(from, src)) {
        if (ok[k] != 0U) {
            peers_incoming_ref(&incoming, e->id, (unsigned)k);
            str_addz(fetched, str_cstr(&incoming));
            if ((git_batch_query(b, str_cstr(&incoming), &in) == GIT_OBJ_COMMIT) &&
                !oid_eq(&in, t)) {
                if (!oidlist_has(cand, &in)) {
                    oidlist_add(cand, &in);
                }
                oidlist_add(&tips->oids, &in);
                str_addz(&tips->src, src);
            }
        }
    }
    str_free(&incoming);
    free(ok);
}

/*
 * In remoto (D-110): l'ultima versione vista del creatore,
 * refs/shared-creator/<id>, va tra le candidate come la versione di un
 * peer, con l'URL come provenienza: se e' da unire, remember ne fa un
 * avviso. Nessuna rete: la ref la aggiornano push, pull e status --remote.
 */
static void from_creator(const SharedEntry_t *e, GitBatch_t *b, const Oid_t *t,
                         OidList_t *cand, Tips_t *tips, Received_t *rv)
{
    Str_t ref = STR_INIT;
    Oid_t seen;

    remote_seen_ref(&ref, e->id);
    if (git_batch_query(b, str_cstr(&ref), &seen) == GIT_OBJ_COMMIT) {
        rv->creator_tip = seen;
        if (!oid_eq(&seen, t)) {
            if (!oidlist_has(cand, &seen)) {
                oidlist_add(cand, &seen);
            }
            oidlist_add(&tips->oids, &seen);
            str_addz(&tips->src, str_cstr(&rv->creator));
        }
    }
    str_free(&ref);
}

/*
 * Una versione da unire arrivata senza avviso (update --all, o la punta di
 * una repo annunciante piu' nuova della versione annunciata) diventa un
 * avviso della repo stessa: la ricordano i comandi successivi, che non
 * fanno il fetch da tutti.
 */
static void remember(const Repo_t *r, const SharedEntry_t *e,
                     const Inbox_t *in, const Tips_t *tips, Received_t *rv)
{
    const char *src;
    size_t h;
    size_t k;

    for (h = 0U; h < rv->heads.n; h++) {
        if (oidlist_has(&in->oids, &rv->heads.v[h])) {
            continue; /* ha gia' il suo avviso */
        }
        for (k = 0U, src = str_znext(&tips->src, NULL); k < tips->oids.n;
             k++, src = str_znext(&tips->src, src)) {
            if (oid_eq(&tips->oids.v[k], &rv->heads.v[h])) {
                (void)inbox_add(str_cstr(&r->top), e->id, &rv->heads.v[h], src);
                add_once(&rv->to_merge, src);
                break;
            }
        }
    }
}

/*
 * merge-base --independent T C1 C2 ...: restano le versioni che nessuna
 * altra contiene. Se T non e' tra queste, una la contiene e T avanza su
 * quella, in fast-forward: non per forza la prima, l'ordine e' quello di
 * arrivo (reportIdea B-47); si chiede a git per ogni rimasta tranne
 * l'ultima, che se nessun'altra la contiene e' lei. Le altre rimaste sono
 * da unire.
 */
static eGsErr_t advance(const SharedEntry_t *e, Oid_t *t, const OidList_t *cand,
                        Received_t *rv)
{
    const char **args = xmalloc((cand->n + 4U) * sizeof *args);
    OidList_t tops = OIDLIST_INIT;
    Str_t argbuf = STR_INIT; /* oid esadecimali degli argomenti */
    Str_t out = STR_INIT;
    Str_t ref = STR_INIT;
    const char *h;
    const char *nl;
    char hex[OID_HEX_SIZE];
    Oid_t in;
    size_t k;
    size_t pick;
    size_t na = 0U;
    eGsErr_t err;

    args[na++] = "merge-base";
    args[na++] = "--independent";
    oid_to_hex(t, hex);
    str_addz(&argbuf, hex);
    for (k = 0U; k < cand->n; k++) {
        oid_to_hex(&cand->v[k], hex);
        str_addz(&argbuf, hex);
    }
    for (h = str_znext(&argbuf, NULL); h != NULL; h = str_znext(&argbuf, h)) {
        args[na++] = h;
    }
    args[na] = NULL;
    err = git_run(args, 0U, &out);
    if (err != GS_OK) {
        goto exit;
    }
    for (h = str_cstr(&out); *h != '\0'; h = (*nl == '\n') ? (nl + 1) : nl) {
        nl = strchr(h, '\n');
        nl = (nl != NULL) ? nl : (h + strlen(h));
        if (oid_from_hex(&in, h, (size_t)(nl - h))) {
            oidlist_add(&tops, &in);
        }
    }
    if ((tops.n > 0U) && !oidlist_has(&tops, t)) {
        pick = tops.n;
        for (k = 0U; (k < tops.n) && (pick == tops.n); k++) {
            if (((k + 1U) == tops.n) || merge_is_ancestor(t, &tops.v[k])) {
                pick = k;
            }
        }
        chain_ref(&ref, e->id);
        err = git_update_ref(str_cstr(&ref), &tops.v[pick], t,
                             "git shared: receive");
        if (err != GS_OK) {
            goto exit;
        }
        rv->advanced = 1;
        *t = tops.v[pick];
    }
    for (k = 0U; k < tops.n; k++) {
        if (!oid_eq(&tops.v[k], t)) {
            oidlist_add(&rv->heads, &tops.v[k]);
        }
    }

exit:
    free((void *)args);
    oidlist_free(&tops);
    str_free(&ref);
    str_free(&out);
    str_free(&argbuf);
    return err;
}

eGsErr_t receive_entry(const Repo_t *r, const SharedEntry_t *e, GitBatch_t *b,
                       int all, Received_t *rv)
{
    Inbox_t in = INBOX_INIT;
    Tips_t tips = { OIDLIST_INIT, STR_INIT };
    OidList_t cand = OIDLIST_INIT;
    Str_t from = STR_INIT;    /* repo da cui fare il fetch */
    Str_t fetched = STR_INIT; /* ref temporanee da cancellare */
    Str_t ref = STR_INIT;
    uint8_t *done = NULL;     /* avvisi gia' trattati */
    const char *who;
    Oid_t t;
    size_t k;
    int fetch_all = all;
    eGsErr_t err = GS_OK;

    rv->advanced = 0;
    rv->heads.n = 0U;
    rv->creator_tip.len = 0U;
    str_reset(&rv->missing);
    str_reset(&rv->to_merge);
    str_reset(&rv->creator);

    chain_ref(&ref, e->id);
    if (git_batch_query(b, str_cstr(&ref), &t) != GIT_OBJ_COMMIT) {
        err = GS_ERR_GIT;
        goto exit;
    }
    inbox_read(r, e->id, &in);

    /* 0. In remoto, l'ultima versione vista del creatore (D-110). Senza la
     * voce "creator" nessun processo. */
    if ((remote_creator_url(b, &t, &rv->creator) == GS_OK) &&
        (rv->creator.len > 0U)) {
        from_creator(e, b, &t, &cand, &tips, rv);
    }
    if ((in.oids.n == 0U) && (all == 0) && (cand.n == 0U)) {
        goto exit; /* il caso comune: nessun processo */
    }
    done = xmalloc((in.oids.n > 0U) ? in.oids.n : 1U);
    memset(done, 0, (in.oids.n > 0U) ? in.oids.n : 1U);

    /* 1. Che cosa c'e' gia' e da dove manca il resto. */
    for (k = 0U, who = str_znext(&in.from, NULL); k < in.oids.n;
         k++, who = str_znext(&in.from, who)) {
        if (oid_eq(&in.oids.v[k], &t)) {
            inbox_drop(r, e->id, &in.oids.v[k]);
            done[k] = 1U;
        } else if (have_commit(b, &in.oids.v[k])) {
            if (!oidlist_has(&cand, &in.oids.v[k])) {
                oidlist_add(&cand, &in.oids.v[k]);
            }
        } else if (str_zhas(&e->peers, who)) {
            add_once(&from, who);
        } else if ((rv->creator.len > 0U) && str_eq(&rv->creator, who)) {
            /* versione del creatore che qui manca: la porta pull */
        } else {
            fetch_all = 1; /* repo sconosciuta: da tutti i peer */
        }
    }

    /* 2. Fetch: un processo per repo, non per avviso. */
    if ((fetch_all != 0) && (e->peers.len > 0U)) {
        str_reset(&from);
        str_addn(&from, e->peers.buf, e->peers.len);
    }
    if (from.len > 0U) {
        fetch_from(e, b, &from, &t, &cand, &tips, &fetched);
        for (k = 0U; k < in.oids.n; k++) {
            if ((done[k] == 0U) && !oidlist_has(&cand, &in.oids.v[k]) &&
                !oid_eq(&in.oids.v[k], &t) && have_commit(b, &in.oids.v[k])) {
                oidlist_add(&cand, &in.oids.v[k]);
            }
        }
    }

    /* 3. T avanza; restano le versioni da unire. */
    if (cand.n > 0U) {
        err = advance(e, &t, &cand, rv);
        if (err != GS_OK) {
            goto exit;
        }
        remember(r, e, &in, &tips, rv);
    }

    /* 4. Avvisi: via quelli che T contiene. */
    for (k = 0U, who = str_znext(&in.from, NULL); k < in.oids.n;
         k++, who = str_znext(&in.from, who)) {
        if (done[k] != 0U) {
            /* gia' cancellato */
        } else if (!have_commit(b, &in.oids.v[k])) {
            add_once(&rv->missing, who); /* fetch fallito: si riprova */
        } else if (oidlist_has(&rv->heads, &in.oids.v[k])) {
            add_once(&rv->to_merge, who);
        } else if (oid_eq(&in.oids.v[k], &t) || (rv->heads.n == 0U) ||
                   merge_is_ancestor(&in.oids.v[k], &t)) {
            /* senza versioni da unire ogni candidata e' in T */
            inbox_drop(r, e->id, &in.oids.v[k]);
        } else {
            add_once(&rv->to_merge, who); /* contenuta in una da unire */
        }
    }

    /* T e' avanzata su una versione arrivata in remoto: da qui il file ha
     * un creatore (lo mostra status; il prossimo comando lo interroga). */
    if (rv->advanced && (rv->creator.len == 0U)) {
        (void)remote_creator_url(b, &t, &rv->creator);
    }

exit:
    if (fetched.len > 0U) {
        /* Gli oggetti restano nella repo: servono alle unioni; un conflitto
         * li lega a refs/shared-merge/<id>. */
        (void)git_delete_refs(&fetched);
    }
    free(done);
    oidlist_free(&tips.oids);
    str_free(&tips.src);
    oidlist_free(&cand);
    inbox_free(&in);
    str_free(&ref);
    str_free(&fetched);
    str_free(&from);
    return err;
}
