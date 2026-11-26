/*
 * receive.c - ricevere le versioni dei peer (D-015, D-092, D-099).
 */
#include "receive.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "auth.h"
#include "chain.h"
#include "inbox.h"
#include "mem.h"
#include "merge.h"
#include "msg.h"
#include "peers.h"
#include "remote.h"

/* Toglie i cambi di autorita' registrati. */
static void changes_free(Received_t *rv)
{
    size_t k;

    for (k = 0U; k < rv->nchanges; k++) {
        str_free(&rv->changes[k].key);
        str_free(&rv->changes[k].authority);
        str_free(&rv->changes[k].from);
    }
    free(rv->changes);
    rv->changes = NULL;
    rv->nchanges = 0U;
}

void received_free(Received_t *rv)
{
    oidlist_free(&rv->heads);
    str_free(&rv->missing);
    str_free(&rv->to_merge);
    str_free(&rv->creator);
    str_free(&rv->authority);
    changes_free(rv);
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
 * In remoto (D-110): l'ultima versione vista, refs/shared-authority/<id>,
 * va tra le candidate come la versione di un peer, con l'URL su cui e'
 * stata vista come provenienza: se e' da unire, remember ne fa un avviso.
 * Nessuna rete: la ref la aggiornano push, pull e status --remote. E' una
 * versione del file, quindi e' candidata comunque; come confronto con
 * l'autorita' di T (auth_tip) vale solo se e' stata vista su di lei
 * (D-112). *seen = la ref, len = 0 se non c'e'.
 */
static void from_authority(const SharedEntry_t *e, GitBatch_t *b, const Oid_t *t,
                           OidList_t *cand, Tips_t *tips, Received_t *rv,
                           Oid_t *seen)
{
    Str_t ref = STR_INIT;

    remote_seen_ref(&ref, e->id);
    if (git_batch_query(b, str_cstr(&ref), seen) == GIT_OBJ_COMMIT) {
        if ((e->seen_url.len > 0U) &&
            remote_same(str_cstr(&e->seen_url), str_cstr(&rv->authority))) {
            rv->auth_tip = *seen;
        }
        if (!oid_eq(seen, t)) {
            if (!oidlist_has(cand, seen)) {
                oidlist_add(cand, seen);
            }
            oidlist_add(&tips->oids, seen);
            str_addz(&tips->src, (e->seen_url.len > 0U) ? str_cstr(&e->seen_url)
                                                        : str_cstr(&rv->authority));
        }
    } else {
        seen->len = 0U;
    }
    str_free(&ref);
}

/*
 * Una versione da unire arrivata senza avviso (update --scan, o la punta di
 * una repo annunciante piu' nuova della versione annunciata) diventa un
 * avviso della repo stessa: la ricordano i comandi successivi, che non
 * fanno il fetch da tutti.
 */
static void remember(const Repo_t *r, const SharedEntry_t *e,
                     const Inbox_t *in, const Tips_t *tips, const OidList_t *held,
                     const Oid_t *seen, Received_t *rv)
{
    const OidList_t *list[2];
    const Oid_t *v;
    const char *src;
    size_t h;
    size_t k;
    unsigned w;

    list[0] = &rv->heads;
    list[1] = held; /* cambi di autorita' da decidere (D-111) */
    for (w = 0U; w < 2U; w++) {
        for (h = 0U; h < list[w]->n; h++) {
            v = &list[w]->v[h];
            if (oidlist_has(&in->oids, v) || ((w == 1U) && oid_eq(v, seen))) {
                continue; /* ha gia' il suo avviso, o la sua ref */
            }
            for (k = 0U, src = str_znext(&tips->src, NULL); k < tips->oids.n;
                 k++, src = str_znext(&tips->src, src)) {
                if (oid_eq(&tips->oids.v[k], v)) {
                    (void)inbox_add(str_cstr(&r->top), e->id, v, src);
                    if (w == 0U) {
                        add_once(&rv->to_merge, src);
                    }
                    break;
                }
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

/* ---- Cambi di autorita' (D-111, REQ-F-030) ------------------------------ */

int receive_auth_listed(const Str_t *keys, const Auth_t *a)
{
    const char *k;

    for (k = str_znext(keys, NULL); k != NULL; k = str_znext(keys, k)) {
        if (auth_key_matches(a, k)) {
            return 1;
        }
    }
    return 0;
}

int receive_auth_accepted(const SharedEntry_t *e, GitBatch_t *b, const Oid_t *v)
{
    Auth_t a = AUTH_INIT;
    int ok = 0;

    if ((v != NULL) && (e->auth_accepted.len > 0U) && (auth_of(b, v, &a, NULL) == GS_OK)) {
        ok = receive_auth_listed(&e->auth_accepted, &a);
    }
    auth_free(&a);
    return ok;
}

/* Che cosa serve sapere di T per giudicare le versioni ricevute. */
typedef struct {
    const SharedEntry_t *e;
    GitBatch_t *b;
    Oid_t t;
    AuthBlobs_t tb; /* voci di T */
    Auth_t ta;      /* voci di T lette (se valide: t_valid) */
    int t_valid;
    Oid_t seen;     /* refs/shared-authority/<id>; len = 0 se non c'e' */
    Str_t hit;      /* valori di authorityAccepted che valgono per una
                     * versione ricevuta */
} Decide_t;

/* Esito per una versione ricevuta. */
typedef enum {
    CAND_KEEP = 0, /* candidata */
    CAND_DROP,     /* non candidata: voci non valide */
    CAND_REJECTED, /* non candidata: autorita' rifiutata */
    CAND_HOLD      /* da decidere: al suo posto *repl (len = 0: nessuna) */
} eCand_t;

static void decide_init(Decide_t *d, const SharedEntry_t *e, GitBatch_t *b,
                        const Oid_t *t, const Oid_t *seen)
{
    Auth_t empty = AUTH_INIT;

    d->e = e;
    d->b = b;
    d->t = *t;
    d->ta = empty;
    auth_blobs(b, t, &d->tb);
    /* Voci di T non valide: come senza creatore (D-107). */
    d->t_valid = (auth_read(b, &d->tb, &d->ta) == GS_OK);
    d->seen = *seen;
    str_init(&d->hit);
}

static void decide_free(Decide_t *d)
{
    auth_free(&d->ta);
    str_free(&d->hit);
}

/* 1 se la versione v ha l'autorita' ref (voci refb, lette in refa). */
static int same_auth(Decide_t *d, const Oid_t *v, const AuthBlobs_t *refb,
                     const Auth_t *refa)
{
    AuthBlobs_t vb;
    Auth_t va = AUTH_INIT;
    int same;

    auth_blobs(d->b, v, &vb);
    if (auth_blobs_eq(&vb, refb)) {
        return 1;
    }
    same = (auth_read(d->b, &vb, &va) == GS_OK) && auth_same(&va, refa);
    auth_free(&va);
    return same;
}

/*
 * L'ultima versione tra base (esclusa) e c che non cambia l'autorita':
 * la prima, in ordine topologico, di rev-list --ancestry-path base..c con
 * l'autorita' di T o, per una versione da unire, della versione comune
 * (D-111, B5). base->len = 0: nessuna versione comune. repl->len = 0 se
 * non c'e'.
 */
static eGsErr_t find_stop(Decide_t *d, const Oid_t *base, const Oid_t *c,
                          const AuthBlobs_t *bb, const Auth_t *ba, Oid_t *repl)
{
    const char *args[] = { "rev-list", "--topo-order", "--ancestry-path", NULL,
                           NULL };
    char hc[OID_HEX_SIZE];
    char hb[OID_HEX_SIZE];
    Str_t range = STR_INIT;
    Str_t out = STR_INIT;
    const char *line;
    const char *nl;
    Oid_t v;
    eGsErr_t err;

    repl->len = 0U;
    oid_to_hex(c, hc);
    if (base->len > 0U) {
        oid_to_hex(base, hb);
        str_addf(&range, "%s..%s", hb, hc);
    } else {
        args[2] = hc; /* nessuna versione comune: tutta la storia di c */
    }
    if (base->len > 0U) {
        args[3] = str_cstr(&range);
    }
    err = git_run(args, 0U, &out);
    for (line = str_cstr(&out); (err == GS_OK) && (*line != '\0') && (repl->len == 0U);
         line = (*nl == '\n') ? (nl + 1) : nl) {
        nl = strchr(line, '\n');
        nl = (nl != NULL) ? nl : (line + strlen(line));
        if (!oid_from_hex(&v, line, (size_t)(nl - line)) || oid_eq(&v, c)) {
            continue;
        }
        if ((d->t_valid && same_auth(d, &v, &d->tb, &d->ta)) ||
            ((bb != NULL) && same_auth(d, &v, bb, ba))) {
            *repl = v;
        }
    }
    str_free(&out);
    str_free(&range);
    return err;
}

/*
 * Giudica la versione ricevuta c (REQ-F-030). Conta l'autorita' che
 * porterebbe a T: la sua se discende da T; se diverge, quella
 * dell'unione a 3 vie, che resta di T se c ha quella della versione
 * comune. In ca le voci di c, in *with_creator se cambia il creatore.
 */
static eCand_t judge(Decide_t *d, const Oid_t *c, Auth_t *ca, int *with_creator,
                     Oid_t *repl)
{
    const SharedEntry_t *e = d->e;
    AuthBlobs_t cb;
    AuthBlobs_t bb;
    Auth_t ba = AUTH_INIT;
    Oid_t mb;
    const char *k;
    int anc;
    int base_ok = 0;
    eCand_t res = CAND_KEEP;

    *with_creator = 0;
    repl->len = 0U;
    mb.len = 0U;
    auth_blobs(d->b, c, &cb);
    if (auth_blobs_eq(&cb, &d->tb)) {
        return CAND_KEEP; /* il caso comune: nessun processo */
    }
    if (auth_read(d->b, &cb, ca) != GS_OK) {
        return CAND_DROP; /* voci non valide: non candidata (D-107) */
    }
    /* Il primo creatore non si sceglie: e' la nascita dell'autorita'. */
    if (!d->t_valid || (d->ta.creator.len == 0U) || auth_same(&d->ta, ca)) {
        return CAND_KEEP;
    }
    anc = merge_is_ancestor(&d->t, c);
    if (!anc) {
        if (merge_base(&d->t, c, &mb) != GS_OK) {
            mb.len = 0U;
        }
        auth_blobs(d->b, (mb.len > 0U) ? &mb : NULL, &bb);
        base_ok = (auth_read(d->b, &bb, &ba) == GS_OK);
        if (base_ok && auth_same(&ba, ca)) {
            goto exit; /* rimasta all'autorita' comune: resta quella di T */
        }
    }
    *with_creator = !auth_creator_same(&d->ta, ca);
    if (receive_auth_listed(&e->auth_rejected, ca)) {
        res = CAND_REJECTED;
    } else if (receive_auth_listed(&e->auth_accepted, ca)) {
        for (k = str_znext(&e->auth_accepted, NULL); k != NULL;
             k = str_znext(&e->auth_accepted, k)) {
            if (auth_key_matches(ca, k) && !str_zhas(&d->hit, k)) {
                str_addz(&d->hit, k);
            }
        }
    } else {
        res = CAND_HOLD;
        if (find_stop(d, anc ? &d->t : &mb, c, (!anc && base_ok) ? &bb : NULL, &ba,
                      repl) != GS_OK) {
            repl->len = 0U;
        }
    }

exit:
    auth_free(&ba);
    return res;
}

/* Registra un cambio da decidere: uno per autorita'. */
static void change_add(Received_t *rv, const Auth_t *ca, int with_creator,
                       const Oid_t *v, const char *from, int by_authority,
                       int rejected)
{
    AuthChange_t *c;
    Str_t key = STR_INIT;
    size_t k;

    auth_key(ca, with_creator, &key);
    for (k = 0U; k < rv->nchanges; k++) {
        if (str_eq(&rv->changes[k].key, str_cstr(&key))) {
            str_free(&key);
            return;
        }
    }
    rv->changes = xrealloc(rv->changes, (rv->nchanges + 1U) * sizeof *rv->changes);
    c = &rv->changes[rv->nchanges++];
    c->key = key;
    str_init(&c->authority);
    str_add(&c->authority, auth_authority(ca));
    str_init(&c->from);
    str_add(&c->from, from);
    c->version = *v;
    c->creator = (with_creator != 0) ? 1U : 0U;
    c->by_authority = (by_authority != 0) ? 1U : 0U;
    c->rejected = (rejected != 0) ? 1U : 0U;
}

size_t receive_pending_changes(const Received_t *rv)
{
    size_t k;
    size_t n = 0U;

    for (k = 0U; k < rv->nchanges; k++) {
        if (rv->changes[k].rejected == 0U) {
            n++;
        }
    }
    return n;
}

/* Da dove viene la versione v: la repo del fetch o dell'avviso. */
static const char *source_of(const Oid_t *v, const Tips_t *tips, const Inbox_t *in)
{
    const char *src;
    size_t k;

    for (k = 0U, src = str_znext(&tips->src, NULL); k < tips->oids.n;
         k++, src = str_znext(&tips->src, src)) {
        if (oid_eq(&tips->oids.v[k], v)) {
            return src;
        }
    }
    for (k = 0U, src = str_znext(&in->from, NULL); k < in->oids.n;
         k++, src = str_znext(&in->from, src)) {
        if (oid_eq(&in->oids.v[k], v)) {
            return src;
        }
    }
    return "";
}

/*
 * Toglie dalle candidate le versioni che cambiano l'autorita' senza una
 * decisione (held, con la versione dove fermarsi al loro posto) e quelle
 * rifiutate o non valide (dropped).
 */
static void decide_filter(Decide_t *d, OidList_t *cand, const Tips_t *tips,
                          const Inbox_t *in, OidList_t *held, OidList_t *dropped,
                          Received_t *rv)
{
    OidList_t keep = OIDLIST_INIT;
    Auth_t ca = AUTH_INIT;
    Oid_t repl;
    size_t k;
    int with_creator;
    eCand_t res;

    for (k = 0U; k < cand->n; k++) {
        res = judge(d, &cand->v[k], &ca, &with_creator, &repl);
        if (res == CAND_KEEP) {
            if (!oidlist_has(&keep, &cand->v[k])) {
                oidlist_add(&keep, &cand->v[k]);
            }
        } else if (res == CAND_DROP) {
            oidlist_add(dropped, &cand->v[k]);
        } else if (res == CAND_REJECTED) {
            oidlist_add(dropped, &cand->v[k]);
            change_add(rv, &ca, with_creator, &cand->v[k],
                       source_of(&cand->v[k], tips, in),
                       (d->seen.len > 0U) && oid_eq(&cand->v[k], &d->seen), 1);
        } else {
            oidlist_add(held, &cand->v[k]);
            change_add(rv, &ca, with_creator, &cand->v[k],
                       source_of(&cand->v[k], tips, in),
                       (d->seen.len > 0U) && oid_eq(&cand->v[k], &d->seen), 0);
            if ((repl.len > 0U) && !oid_eq(&repl, &d->t) && !oidlist_has(&keep, &repl)) {
                oidlist_add(&keep, &repl);
            }
        }
    }
    cand->n = 0U;
    for (k = 0U; k < keep.n; k++) {
        oidlist_add(cand, &keep.v[k]);
    }
    auth_free(&ca);
    oidlist_free(&keep);
}

/*
 * Pulizia delle decisioni (D-111): authorityAccepted vale una volta, e si
 * toglie quando T ha quell'autorita' o nessuna versione ricevuta ce l'ha
 * piu'; authorityRejected si toglie quando T arriva comunque a
 * quell'autorita'. ta: le voci di T adesso (valid = 0: non lette).
 */
static void decide_cleanup(const Decide_t *d, const Auth_t *ta, int valid)
{
    const SharedEntry_t *e = d->e;
    const char *k;

    for (k = str_znext(&e->auth_accepted, NULL); k != NULL;
         k = str_znext(&e->auth_accepted, k)) {
        if ((valid && auth_key_matches(ta, k)) || !str_zhas(&d->hit, k)) {
            (void)repo_auth_drop(e->id, 1, k);
        }
    }
    for (k = str_znext(&e->auth_rejected, NULL); (k != NULL) && valid;
         k = str_znext(&e->auth_rejected, k)) {
        if (auth_key_matches(ta, k)) {
            (void)repo_auth_drop(e->id, 0, k);
        }
    }
}

/* Creatore, autorita' e ultima versione vista di T in rv. */
static void set_authority(const SharedEntry_t *e, GitBatch_t *b, const Oid_t *t,
                          const Oid_t *seen, Received_t *rv)
{
    Auth_t a = AUTH_INIT;

    str_reset(&rv->creator);
    str_reset(&rv->authority);
    rv->sovereign = 0;
    rv->auth_tip.len = 0U;
    if ((auth_of(b, t, &a, NULL) == GS_OK) && (a.creator.len > 0U)) {
        str_add(&rv->creator, str_cstr(&a.creator));
        str_add(&rv->authority, auth_authority(&a));
        rv->sovereign = (a.n > 0U) && !remote_same(auth_authority(&a), str_cstr(&a.creator));
        if ((seen->len > 0U) && (e->seen_url.len > 0U) &&
            remote_same(str_cstr(&e->seen_url), str_cstr(&rv->authority))) {
            rv->auth_tip = *seen;
        }
    }
    auth_free(&a);
}

eGsErr_t receive_restore_chain(const SharedEntry_t *e, GitBatch_t *b, Oid_t *t)
{
    PeerTally_t tally = { 0U, 0U, 0U };
    size_t n = str_zcount(&e->peers);
    uint8_t *ok = xmalloc((n > 0U) ? n : 1U);
    const char *rp[] = { "rev-parse", "--verify", "-q", NULL, NULL };
    const char *p;
    Str_t ref = STR_INIT;
    Str_t src = STR_INIT;
    Str_t del = STR_INIT;
    Str_t out = STR_INIT;
    Oid_t tip;
    size_t k;
    int found = 0;
    eGsErr_t err = GS_ERR_GIT;

    /* 1. L'ultima versione vista in remoto: nessuna rete. Il suo tree dice
     * anche il creatore, quindi il prossimo pull sa dove andare. */
    remote_seen_ref(&ref, e->id);
    if (git_batch_query(b, str_cstr(&ref), &tip) == GIT_OBJ_COMMIT) {
        found = 1;
        if (e->seen_url.len > 0U) {
            str_add(&src, str_cstr(&e->seen_url));
        } else if ((remote_creator_url(b, &tip, &src) != GS_OK) ||
                   (src.len == 0U)) {
            str_reset(&src);
            str_add(&src, str_cstr(&ref));
        } else {
            /* URL del creatore */
        }
    }
    /* 2. I peer locali: il primo che ha la catena. */
    if ((found == 0) && (n > 0U)) {
        peers_fetch(str_cstr(&e->path), e->id, &e->peers, ok, &tally);
        for (k = 0U, p = str_znext(&e->peers, NULL); k < n;
             k++, p = str_znext(&e->peers, p)) {
            if (ok[k] == 0U) {
                continue;
            }
            peers_incoming_ref(&ref, e->id, (unsigned)k);
            str_addz(&del, str_cstr(&ref));
            rp[3] = str_cstr(&ref);
            if ((found == 0) && (git_run(rp, PROC_STDERR_NULL, &out) == GS_OK) &&
                oid_from_hex(&tip, out.buf, out.len)) {
                found = 1;
                str_add(&src, p);
            }
        }
    }
    if (found != 0) {
        chain_ref(&ref, e->id);
        err = git_update_ref(str_cstr(&ref), &tip, NULL, "git shared: chain restored");
    }
    if (del.len > 0U) {
        (void)git_delete_refs(&del);
    }
    if (err == GS_OK) {
        msg_warning(MSG_WARN_CHAIN_RESTORED, str_cstr(&e->path), str_cstr(&src));
        *t = tip;
        /* Il cat-file aperto ha gia' visto la ref mancare e non la
         * rileggerebbe: si riapre. */
        git_batch_close(b);
        err = git_batch_open(b);
    }
    str_free(&out);
    str_free(&del);
    str_free(&src);
    str_free(&ref);
    free(ok);
    return err;
}

/* Azzera rv per una nuova ricezione. */
static void received_reset(Received_t *rv)
{
    rv->advanced = 0;
    rv->heads.n = 0U;
    rv->auth_tip.len = 0U;
    rv->sovereign = 0;
    str_reset(&rv->missing);
    str_reset(&rv->to_merge);
    str_reset(&rv->creator);
    str_reset(&rv->authority);
    changes_free(rv);
}

eGsErr_t receive_entry(const Repo_t *r, const SharedEntry_t *e, GitBatch_t *b,
                       int all, Received_t *rv)
{
    Inbox_t in = INBOX_INIT;
    Tips_t tips = { OIDLIST_INIT, STR_INIT };
    OidList_t cand = OIDLIST_INIT;
    OidList_t held = OIDLIST_INIT;    /* cambi di autorita' da decidere */
    OidList_t dropped = OIDLIST_INIT; /* autorita' rifiutate, voci non valide */
    Decide_t d;
    Auth_t ta = AUTH_INIT;
    Str_t from = STR_INIT;    /* repo da cui fare il fetch */
    Str_t fetched = STR_INIT; /* ref temporanee da cancellare */
    Str_t ref = STR_INIT;
    uint8_t *done = NULL;     /* avvisi gia' trattati */
    const char *who;
    Oid_t t;
    Oid_t seen;
    size_t k;
    int fetch_all = all;
    int decided = 0;
    eGsErr_t err = GS_OK;

    received_reset(rv);
    seen.len = 0U;
    chain_ref(&ref, e->id);
    if ((git_batch_query(b, str_cstr(&ref), &t) != GIT_OBJ_COMMIT) &&
        (receive_restore_chain(e, b, &t) != GS_OK)) {
        err = GS_ERR_GIT; /* B-51: nessuno ha la catena */
        goto exit;
    }
    inbox_read(r, e->id, &in);

    /* 0. In remoto, l'ultima versione vista (D-110). Senza la voce
     * "creator" nessun processo. */
    set_authority(e, b, &t, &seen, rv);
    if (rv->creator.len > 0U) {
        from_authority(e, b, &t, &cand, &tips, rv, &seen);
    }
    decide_init(&d, e, b, &t, &seen);
    decided = 1;
    if ((in.oids.n == 0U) && (all == 0) && (cand.n == 0U)) {
        goto cleanup; /* il caso comune: nessun processo */
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
        } else if ((rv->authority.len > 0U) && str_eq(&rv->authority, who)) {
            /* versione dell'autorita' che qui manca: la porta pull */
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

    /* 3. Cambi di autorita' (D-111): prima si decide, poi si riceve. */
    decide_filter(&d, &cand, &tips, &in, &held, &dropped, rv);

    /* 4. T avanza; restano le versioni da unire. */
    if (cand.n > 0U) {
        err = advance(e, &t, &cand, rv);
        if (err != GS_OK) {
            goto exit;
        }
    }
    remember(r, e, &in, &tips, &held, &seen, rv);

    /* 5. Avvisi: via quelli che T contiene e quelli rifiutati; restano
     * quelli da unire e da decidere. */
    for (k = 0U, who = str_znext(&in.from, NULL); k < in.oids.n;
         k++, who = str_znext(&in.from, who)) {
        if (done[k] != 0U) {
            /* gia' cancellato */
        } else if (!have_commit(b, &in.oids.v[k])) {
            add_once(&rv->missing, who); /* fetch fallito: si riprova */
        } else if (oidlist_has(&dropped, &in.oids.v[k])) {
            inbox_drop(r, e->id, &in.oids.v[k]); /* mai candidata */
        } else if (oidlist_has(&held, &in.oids.v[k])) {
            /* da decidere: la riga "authority" di status */
        } else if (oidlist_has(&rv->heads, &in.oids.v[k])) {
            add_once(&rv->to_merge, who);
        } else if (oid_eq(&in.oids.v[k], &t) ||
                   ((rv->heads.n == 0U) && (held.n == 0U)) ||
                   merge_is_ancestor(&in.oids.v[k], &t)) {
            /* senza versioni da unire ne' da decidere ogni candidata e'
             * in T */
            inbox_drop(r, e->id, &in.oids.v[k]);
        } else {
            add_once(&rv->to_merge, who); /* contenuta in una da unire */
        }
    }

    /* T e' avanzata, forse su una versione arrivata in remoto: creatore e
     * autorita' possono essere nuovi (lo mostra status). */
    if (rv->advanced) {
        set_authority(e, b, &t, &seen, rv);
    }

cleanup:
    if ((e->auth_accepted.len > 0U) || (e->auth_rejected.len > 0U)) {
        decide_cleanup(&d, &ta, auth_of(b, &t, &ta, NULL) == GS_OK);
    }

exit:
    if (fetched.len > 0U) {
        /* Gli oggetti restano nella repo: servono alle unioni; un conflitto
         * li lega a refs/shared-merge/<id>. */
        (void)git_delete_refs(&fetched);
    }
    if (decided != 0) {
        decide_free(&d);
    }
    free(done);
    auth_free(&ta);
    oidlist_free(&tips.oids);
    str_free(&tips.src);
    oidlist_free(&cand);
    oidlist_free(&held);
    oidlist_free(&dropped);
    inbox_free(&in);
    str_free(&ref);
    str_free(&fetched);
    str_free(&from);
    return err;
}

eGsErr_t receive_pending_auth(const Repo_t *r, const SharedEntry_t *e,
                              GitBatch_t *b, Received_t *rv)
{
    Inbox_t in = INBOX_INIT;
    Tips_t tips = { OIDLIST_INIT, STR_INIT };
    OidList_t cand = OIDLIST_INIT;
    OidList_t held = OIDLIST_INIT;
    OidList_t dropped = OIDLIST_INIT;
    Decide_t d;
    Str_t ref = STR_INIT;
    Oid_t t;
    Oid_t seen;
    size_t k;
    eGsErr_t err = GS_OK;

    received_reset(rv);
    seen.len = 0U;
    chain_ref(&ref, e->id);
    if (git_batch_query(b, str_cstr(&ref), &t) != GIT_OBJ_COMMIT) {
        err = GS_ERR_GIT;
        goto exit;
    }
    set_authority(e, b, &t, &seen, rv);
    if (rv->creator.len == 0U) {
        goto exit; /* senza creatore nessun cambio da decidere */
    }
    from_authority(e, b, &t, &cand, &tips, rv, &seen);
    inbox_read(r, e->id, &in);
    for (k = 0U; k < in.oids.n; k++) {
        if (!oid_eq(&in.oids.v[k], &t) && have_commit(b, &in.oids.v[k]) &&
            !oidlist_has(&cand, &in.oids.v[k])) {
            oidlist_add(&cand, &in.oids.v[k]);
        }
    }
    decide_init(&d, e, b, &t, &seen);
    decide_filter(&d, &cand, &tips, &in, &held, &dropped, rv);
    decide_free(&d);

exit:
    oidlist_free(&tips.oids);
    str_free(&tips.src);
    oidlist_free(&cand);
    oidlist_free(&held);
    oidlist_free(&dropped);
    inbox_free(&in);
    str_free(&ref);
    return err;
}
