/*
 * cmd_crown.c - "git shared crown" (REQ-F-029, REQ-F-030, D-111).
 *
 * Tutto cio' che riguarda l'autorita' di un file, cioe' la repo remota da
 * cui arrivano le versioni ufficiali (auth.h). Il creatore non cambia mai:
 * l'autorita' passa a un sovrano.
 *
 *   crown [--yes] [<remote|url>] <path>
 *       senza <remote|url> vale origin, la propria repo remota (D-114).
 *       Se all'URL c'e' gia' un'autorita' del file (la sua ultima versione
 *       dichiara l'URL stesso), la SEGUE: nessun permesso, come ogni
 *       lettore. Altrimenti la INSEDIA: versione nuova con il tree di T e
 *       l'URL in fondo ai sovrani, genitori T e, se l'URL ha versioni che
 *       T non contiene, la sua punta (merge -s ours: il push e' un
 *       fast-forward); push prima sull'URL, poi la catena locale, poi in
 *       fast-forward sul vecchio sovrano (passaggio di consegne).
 *   crown [--yes] --restore <path>
 *       ripristino di fabbrica: segue la linea del creatore. Creatore
 *       irraggiungibile o senza il file: errore.
 *   crown [--yes] --accept [<url>] <path>
 *   crown --reject [<url>] <path>
 *       decide un cambio di autorita' ricevuto (REQ-F-030), senza rete.
 *
 * Prima di scrivere un avviso dice da dove a dove, quando l'autorita' e'
 * stata vista l'ultima volta e quanti peer locali dovranno accettare, e
 * chiede [y/N]; senza terminale il comando rifiuta, salvo --yes. Mai
 * automatico. --reject non chiede: e' la direzione sicura.
 *
 * Gli eredi (D-114, heirs.h): cmd_heirs_visit, per pull e status --remote,
 * prova gli eredi nell'ordine di successione quando l'autorita' non
 * risponde; un erede che si e' incoronato si segue senza domande (follow
 * con quiet). Insediare toglie gli eredi di un sovrano uscito dall'elenco.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "auth.h"
#include "chain.h"
#include "cmd.h"
#include "cmd_common.h"
#include "heirs.h"
#include "mem.h"
#include "merge.h"
#include "peers.h"
#include "receive.h"
#include "remote.h"
#include "sys.h"

typedef enum {
    CROWN_URL = 0, /* crown <remote|url> <path> */
    CROWN_RESTORE,
    CROWN_ACCEPT,
    CROWN_REJECT
} eCrownMode_t;

/* Ciò che serve a seguire o insediare. */
typedef struct {
    Repo_t *r;
    SharedEntry_t *e;
    GitBatch_t *b;
    Received_t *rv;  /* ricezione appena fatta: creatore, autorita' di T */
    Oid_t t;         /* T */
    int yes;         /* --yes */
    int quiet;       /* niente avviso ne' domanda: un erede seguito da
                      * solo (D-114) */
    int batch_ok;    /* 0 se il cat-file non si e' riaperto */
} Crown_t;

/* "AAAA-MM-GG hh:mm" di un istante, o "never" se 0. */
static void seen_text(unsigned long at, Str_t *out)
{
    time_t tt = (time_t)at;
    const struct tm *tm = (at > 0UL) ? localtime(&tt) : NULL;
    char buf[32];

    str_reset(out);
    if ((tm != NULL) && (strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", tm) > 0U)) {
        str_addf(out, msg(MSG_CROWN_LAST_SEEN), buf);
    } else {
        str_add(out, msg(MSG_CROWN_NEVER_SEEN));
    }
}

/*
 * Conferma (D-111): 1 = si', 0 = no (nulla cambia), -1 = nessun terminale
 * a cui chiedere (errore stampato). --yes vale come si'.
 */
static int confirm(int yes, const char *label)
{
    char line[16];

    if (yes != 0) {
        return 1;
    }
    if (!sys_stdin_is_tty()) {
        msg_error(MSG_ERR_CROWN_NO_TTY, label);
        return -1;
    }
    fputs(msg(MSG_CROWN_CONTINUE), stdout);
    (void)fflush(stdout);
    if ((fgets(line, sizeof line, stdin) == NULL) ||
        ((line[0] != 'y') && (line[0] != 'Y'))) {
        printf(msg(MSG_CROWN_CANCELLED), label);
        putchar('\n');
        return 0;
    }
    return 1;
}

/* Codice di uscita di una conferma negata o impossibile. */
static int refused(int c)
{
    return (c < 0) ? GS_EXIT_ERROR : GS_EXIT_OK;
}

/* Prima riga dell'avviso: da quale autorita' a quale. */
static void notice_from(const Crown_t *c, const char *to)
{
    Str_t seen = STR_INIT;

    seen_text(remote_same(str_cstr(&c->e->seen_url), str_cstr(&c->rv->authority))
                  ? c->e->seen_at
                  : 0UL,
              &seen);
    printf(msg(MSG_CROWN_NOTICE_FROM), str_cstr(&c->e->path),
           str_cstr(&c->rv->authority), str_cstr(&seen), to);
    putchar('\n');
    str_free(&seen);
}

/* Ultima riga dell'avviso: i peer locali che dovranno accettare. */
static void notice_peers(const Crown_t *c)
{
    unsigned n = (unsigned)str_zcount(&c->e->peers);

    if (n > 0U) {
        printf(msg(MSG_CROWN_NOTICE_PEERS), n);
        putchar('\n');
    }
}

/*
 * Catena dell'URL in una ref temporanea; la punta in *tip (len = 0 se
 * l'URL non risponde o non ha il file). La ref resta finche' il chiamante
 * non la cancella con drop_tmp: gli oggetti servono. Il cat-file si
 * riapre, perche' veda gli oggetti nuovi.
 */
static void fetch_tip(Crown_t *c, const char *url, Oid_t *tip)
{
    const char *rp[] = { "rev-parse", "--verify", "-q", NULL, NULL };
    Str_t ref = STR_INIT;
    Str_t out = STR_INIT;

    tip->len = 0U;
    peers_incoming_ref(&ref, c->e->id, 0U);
    rp[3] = str_cstr(&ref);
    if ((remote_fetch(url, c->e->id, str_cstr(&ref)) == GS_OK) &&
        (git_run(rp, PROC_STDERR_NULL, &out) == GS_OK) &&
        !oid_from_hex(tip, out.buf, out.len)) {
        tip->len = 0U;
    }
    git_batch_close(c->b);
    if (git_batch_open(c->b) != GS_OK) {
        /* le domande successive falliscono: il comando si ferma li' */
        msg_fatal(MSG_ERR_PROC, "git cat-file");
        c->batch_ok = 0;
    }
    str_free(&out);
    str_free(&ref);
}

static void drop_tmp(const Crown_t *c)
{
    Str_t ref = STR_INIT;
    Str_t del = STR_INIT;

    peers_incoming_ref(&ref, c->e->id, 0U);
    str_addz(&del, str_cstr(&ref));
    (void)git_delete_refs(&del);
    str_free(&del);
    str_free(&ref);
}

/* Toglie un valore da un elenco compatto in memoria. */
static void zremove(Str_t *list, const char *value)
{
    Str_t keep = STR_INIT;
    const char *p;

    for (p = str_znext(list, NULL); p != NULL; p = str_znext(list, p)) {
        if (strcmp(p, value) != 0) {
            str_addz(&keep, p);
        }
    }
    str_reset(list);
    if (keep.len > 0U) {
        str_addn(list, keep.buf, keep.len);
    }
    str_free(&keep);
}

/* Registra la decisione (D-111): un valore non sta mai in entrambe. */
static eGsErr_t record(SharedEntry_t *e, int accepted, const char *key)
{
    eGsErr_t err = repo_auth_drop(e->id, !accepted, key);

    zremove((accepted != 0) ? &e->auth_rejected : &e->auth_accepted, key);
    if (err == GS_OK) {
        err = repo_auth_drop(e->id, accepted, key); /* niente doppioni */
    }
    zremove((accepted != 0) ? &e->auth_accepted : &e->auth_rejected, key);
    if (err == GS_OK) {
        err = repo_auth_add(e, accepted, key);
    }
    return err;
}

/* Di nuovo la ricezione, dopo una decisione: T avanza, o la versione
 * diventa da unire. Lo dice. all: anche dai peer che non hanno annunciato
 * nulla (le versioni di un'autorita' prima rifiutata). */
static int receive_again(Crown_t *c, int all)
{
    const char *label = str_cstr(&c->e->path);

    if (receive_entry(c->r, c->e, c->b, all, c->rv) != GS_OK) {
        msg_error(MSG_ERR_CHAIN, str_cstr(&c->e->name));
        return GS_EXIT_ERROR;
    }
    if (c->rv->advanced != 0) {
        printf(msg(MSG_UPD_RECEIVED), label);
        putchar('\n');
    }
    if (c->rv->heads.n > 0U) {
        printf(msg(MSG_CROWN_TO_MERGE), label);
        putchar('\n');
    }
    return GS_EXIT_OK;
}

/*
 * Seguire (D-111): l'autorita' dichiarata dalla versione u, vista su
 * `seen_url`, diventa quella del file, senza permessi. T piu' vecchia ->
 * fast-forward; divergente -> u da unire; T piu' avanti -> una versione
 * con il contenuto di T e le voci di u, che push proporra'.
 */
static int follow(Crown_t *c, const char *seen_url, const Oid_t *u, const Auth_t *au,
                  int line)
{
    const char *label = str_cstr(&c->e->path);
    const char *to = auth_authority(au);
    Str_t key = STR_INIT;
    Str_t ref = STR_INIT;
    AuthBlobs_t ub;
    Oid_t blob;
    Oid_t v;
    char hex[OID_HEX_SIZE];
    int ok;
    int rc = GS_EXIT_ERROR;

    if (c->quiet == 0) {
        notice_from(c, to);
        if (line != 0) {
            printf(msg(MSG_CROWN_NOTICE_LINE), to);
            putchar('\n');
        }
        printf(msg(MSG_CROWN_NOTICE_FOLLOW), to);
        putchar('\n');
        notice_peers(c);
        ok = confirm(c->yes, label);
        if (ok <= 0) {
            rc = refused(ok);
            goto exit;
        }
    }
    /* L'autorita' accettata, e la versione vista su quell'URL. */
    auth_key(au, 0, &key); /* lo stesso creatore: controllato prima */
    remote_seen_ref(&ref, c->e->id);
    if ((record(c->e, 1, str_cstr(&key)) != GS_OK) ||
        (git_set_ref(str_cstr(&ref), u, "git shared: crown") != GS_OK) ||
        (remote_seen_write(c->e->id, seen_url, &c->e->seen_url, &c->e->seen_at) != GS_OK)) {
        msg_error(MSG_ERR_CHAIN, str_cstr(&c->e->name));
        goto exit;
    }
    if (!oid_eq(u, &c->t) && merge_is_ancestor(u, &c->t)) {
        /* T piu' avanti: le versioni fatte qui passano all'autorita'
         * seguita. L'autorita' cambia subito; il contenuto resta una
         * proposta finche' push non riesce. */
        auth_blobs(c->b, u, &ub);
        oid_to_hex(&c->t, hex);
        str_reset(&ref);
        str_addf(&ref, "%s:content", hex);
        if (!git_batch_blob(c->b, str_cstr(&ref), &blob) ||
            (chain_commit(&blob, &c->t, NULL, &ub, str_cstr(&c->e->name), &v) != GS_OK)) {
            msg_error(MSG_ERR_PUBLISH, label);
            goto exit;
        }
        chain_ref(&ref, c->e->id);
        if (git_update_ref(str_cstr(&ref), &v, &c->t, "git shared: crown") != GS_OK) {
            msg_error(MSG_ERR_CHAIN, str_cstr(&c->e->name));
            goto exit;
        }
        c->t = v;
        printf(msg(MSG_CROWN_FOLLOWED), label, to);
        putchar('\n');
        printf(msg(MSG_CROWN_FOLLOW_AHEAD), label, to);
        putchar('\n');
        rc = receive_again(c, 0); /* toglie authorityAccepted: T ce l'ha */
        if (rc == GS_EXIT_OK) {
            rc = cmd_publish_entry(c->r, c->e, c->b, NULL, &v);
        }
        goto exit;
    }
    rc = receive_again(c, 0);
    if (rc == GS_EXIT_OK) {
        printf(msg(MSG_CROWN_FOLLOWED), label, to);
        putchar('\n');
    }

exit:
    str_free(&ref);
    str_free(&key);
    return rc;
}

/*
 * Insediare (D-111): url diventa il sovrano del file. u = la punta
 * dell'URL (len = 0 se non ha il file).
 */
static int install(Crown_t *c, const char *url, const Oid_t *u)
{
    const char *rl[] = { "rev-list", "--count", NULL, "--not", NULL, NULL };
    const char *label = str_cstr(&c->e->path);
    const char *old = str_cstr(&c->rv->authority);
    char hu[OID_HEX_SIZE];
    char ht[OID_HEX_SIZE];
    Auth_t ta = AUTH_INIT;
    Auth_t na = AUTH_INIT;
    Heirs_t h = HEIRS_INIT;
    AuthBlobs_t tb;
    AuthBlobs_t nb;
    Str_t ref = STR_INIT;
    Str_t out = STR_INIT;
    Oid_t blob;
    Oid_t v;
    const Oid_t *p2 = NULL;
    unsigned long lost = 0UL;
    int ok;
    int rc = GS_EXIT_ERROR;

    /* Genitori: T e, se l'URL ha versioni che T non contiene, la sua
     * punta. Il loro contenuto si perde: l'avviso lo dice. */
    oid_to_hex(&c->t, ht);
    if ((u->len > 0U) && !merge_is_ancestor(u, &c->t)) {
        p2 = u;
        oid_to_hex(u, hu);
        rl[2] = hu;
        rl[4] = ht;
        if (git_run(rl, 0U, &out) == GS_OK) {
            lost = strtoul(str_cstr(&out), NULL, 10);
        }
    }
    if ((auth_of(c->b, &c->t, &ta, &tb) != GS_OK) ||
        (auth_crown(&ta, &tb, url, &nb) != GS_OK)) {
        msg_error(MSG_ERR_PUBLISH, label);
        goto exit;
    }
    /* Gli eredi di un sovrano uscito dall'elenco escono con lui (D-114). */
    if ((nb.heirs.len > 0U) && (heirs_read(c->b, &nb.heirs, &ta, &h) == GS_OK) &&
        (auth_read(c->b, &nb, &na) == GS_OK) && heirs_prune(&h, &na) &&
        (heirs_write(&h, &nb.heirs) != GS_OK)) {
        msg_error(MSG_ERR_PUBLISH, label);
        goto exit;
    }
    notice_from(c, url);
    printf(msg(MSG_CROWN_NOTICE_INSTALL), url);
    putchar('\n');
    if (lost > 0UL) {
        printf(msg(MSG_CROWN_NOTICE_LOST), url, lost);
        putchar('\n');
    }
    notice_peers(c);
    ok = confirm(c->yes, label);
    if (ok <= 0) {
        rc = refused(ok);
        goto exit;
    }
    str_addf(&ref, "%s:content", ht);
    if (!git_batch_blob(c->b, str_cstr(&ref), &blob) ||
        (chain_commit(&blob, &c->t, p2, &nb, str_cstr(&c->e->name), &v) != GS_OK)) {
        msg_error(MSG_ERR_PUBLISH, label);
        goto exit;
    }
    /* Prima sull'URL, senza forzare: se non riesce non cambia nulla. */
    switch (remote_push(url, c->e->id, &v)) {
    case REMOTE_PUSH_OK:
        break;
    case REMOTE_PUSH_NEWER:
        /* P-03 (D-112): un insediamento riuscito a cui e' mancato
         * l'update-ref locale */
        if (remote_find_own(c->b, url, c->e->id, &c->t, p2, &nb, &v)) {
            break;
        }
        msg_error(MSG_ERR_CROWN_PUSH, label, url);
        goto exit;
    default:
        msg_error(MSG_ERR_CROWN_PUSH, label, url);
        goto exit;
    }
    chain_ref(&ref, c->e->id);
    if (git_update_ref(str_cstr(&ref), &v, &c->t, "git shared: crown") != GS_OK) {
        msg_error(MSG_ERR_CHAIN, str_cstr(&c->e->name));
        goto exit;
    }
    remote_seen_ref(&ref, c->e->id);
    if (git_set_ref(str_cstr(&ref), &v, "git shared: crown") == GS_OK) {
        (void)remote_seen_write(c->e->id, url, &c->e->seen_url, &c->e->seen_at);
    }
    printf(msg(MSG_CROWN_INSTALLED), label, url);
    putchar('\n');
    /* Passaggio di consegne: anche sul vecchio sovrano, in fast-forward,
     * cosi' chi lo segue lo sa. */
    if (remote_push(old, c->e->id, &v) == REMOTE_PUSH_OK) {
        printf(msg(MSG_CROWN_OLD_TOLD), label, old);
        putchar('\n');
    } else {
        msg_warning(MSG_WARN_CROWN_OLD_NOT_TOLD, label, old);
    }
    rc = cmd_publish_entry(c->r, c->e, c->b, NULL, &v); /* i peer locali */

exit:
    heirs_free(&h);
    auth_free(&na);
    auth_free(&ta);
    str_free(&out);
    str_free(&ref);
    return rc;
}

/* La punta dell'URL e le sue voci, controllate: stesso file (stesso
 * creatore), voci valide. 1 se c'e' e va bene; 0 se non c'e'; -1 se c'e'
 * ma non va bene (errore stampato). */
static int remote_side(Crown_t *c, const char *url, Oid_t *u, Auth_t *au)
{
    Auth_t ta = AUTH_INIT;
    int res = 1;

    fetch_tip(c, url, u);
    if (u->len == 0U) {
        return 0;
    }
    if (auth_of(c->b, u, au, NULL) != GS_OK) {
        msg_error(MSG_ERR_CROWN_BAD_CHAIN, str_cstr(&c->e->path), url);
        res = -1;
    } else if ((auth_of(c->b, &c->t, &ta, NULL) == GS_OK) && !auth_creator_same(&ta, au)) {
        /* Il creatore non si cambia: un'altra famiglia, non lo stesso
         * file (D-111). */
        msg_error(MSG_ERR_CROWN_OTHER_CREATOR, str_cstr(&c->e->path), url,
                  str_cstr(&au->creator));
        res = -1;
    } else {
        /* stesso file */
    }
    auth_free(&ta);
    return res;
}

/* crown <remote|url> <path> */
static int crown_url(Crown_t *c, const char *arg)
{
    Str_t url = STR_INIT;
    Str_t cwd = STR_INIT;
    Auth_t au = AUTH_INIT;
    Oid_t u;
    int side;
    int rc = GS_EXIT_ERROR;

    str_addf(&cwd, "%s/%s", str_cstr(&c->r->top), str_cstr(&c->r->prefix));
    if (remote_arg_url(str_cstr(&c->r->top), str_cstr(&cwd), arg, &url) != GS_OK) {
        msg_error(MSG_ERR_CROWN_BAD_URL, arg);
        goto exit;
    }
    if (remote_same(str_cstr(&url), str_cstr(&c->rv->authority))) {
        printf(msg(MSG_CROWN_ALREADY), str_cstr(&c->e->path), str_cstr(&url));
        putchar('\n');
        rc = GS_EXIT_OK;
        goto exit;
    }
    side = remote_side(c, str_cstr(&url), &u, &au);
    if (side < 0) {
        goto exit;
    }
    if ((side > 0) && remote_same(auth_authority(&au), str_cstr(&url))) {
        rc = follow(c, str_cstr(&url), &u, &au, 0);
    } else {
        rc = install(c, str_cstr(&url), &u);
    }

exit:
    drop_tmp(c);
    auth_free(&au);
    str_free(&cwd);
    str_free(&url);
    return rc;
}

/* crown --restore <path>: la linea del creatore. */
static int crown_restore(Crown_t *c)
{
    const char *label = str_cstr(&c->e->path);
    const char *creator = str_cstr(&c->rv->creator);
    const char *line;
    Auth_t ac = AUTH_INIT;
    Auth_t aa = AUTH_INIT;
    Str_t to = STR_INIT;
    Oid_t uc;
    Oid_t ua;
    int side;
    int rc = GS_EXIT_ERROR;

    side = remote_side(c, creator, &uc, &ac);
    if (side == 0) {
        msg_error(MSG_ERR_CROWN_RESTORE, label, creator);
    }
    if (side <= 0) {
        goto exit;
    }
    line = auth_authority(&ac);
    if (remote_same(line, str_cstr(&c->rv->authority))) {
        printf(msg(MSG_CROWN_RESTORE_ALREADY), label, line);
        putchar('\n');
        rc = GS_EXIT_OK;
        goto exit;
    }
    if (remote_same(line, creator)) {
        rc = follow(c, creator, &uc, &ac, 0);
        goto exit;
    }
    /* Il creatore ha passato le consegne: si segue chi ha scelto lui, dalla
     * sua catena se risponde, altrimenti da quella del creatore. */
    str_add(&to, line);
    drop_tmp(c);
    side = remote_side(c, str_cstr(&to), &ua, &aa);
    if ((side > 0) && remote_same(auth_authority(&aa), str_cstr(&to))) {
        rc = follow(c, str_cstr(&to), &ua, &aa, 1);
    } else if (side >= 0) {
        fetch_tip(c, creator, &uc); /* di nuovo la ref temporanea */
        rc = follow(c, creator, &uc, &ac, 1);
    } else {
        /* errore stampato */
    }

exit:
    drop_tmp(c);
    auth_free(&aa);
    auth_free(&ac);
    str_free(&to);
    return rc;
}

/* 1 se tra le autorita' rifiutate c'e' url. */
static int rejected_had(const SharedEntry_t *e, const char *url)
{
    Str_t a = STR_INIT;
    const char *k;
    int found = 0;

    for (k = str_znext(&e->auth_rejected, NULL); (k != NULL) && (found == 0);
         k = str_znext(&e->auth_rejected, k)) {
        auth_key_authority(k, &a);
        found = remote_same(str_cstr(&a), url);
    }
    str_free(&a);
    return found;
}

/*
 * --accept <url> di un'autorita' rifiutata le cui versioni non sono piu'
 * qui: il --reject ha cancellato i loro avvisi. Si toglie il rifiuto, si
 * accetta l'autorita' e si riceve da tutti i peer, che le hanno ancora.
 */
static int revive(Crown_t *c, const char *url)
{
    Str_t a = STR_INIT;
    Str_t drop = STR_INIT;
    const char *k;
    int rc = GS_EXIT_ERROR;

    for (k = str_znext(&c->e->auth_rejected, NULL); k != NULL;
         k = str_znext(&c->e->auth_rejected, k)) {
        auth_key_authority(k, &a);
        if (remote_same(str_cstr(&a), url)) {
            str_addz(&drop, k);
        }
    }
    for (k = str_znext(&drop, NULL); k != NULL; k = str_znext(&drop, k)) {
        if (repo_auth_drop(c->e->id, 0, k) != GS_OK) {
            goto exit;
        }
        zremove(&c->e->auth_rejected, k);
    }
    if (record(c->e, 1, url) != GS_OK) {
        goto exit;
    }
    printf(msg(MSG_CROWN_ACCEPTED), str_cstr(&c->e->path), url);
    putchar('\n');
    rc = receive_again(c, 1);

exit:
    if (rc == GS_EXIT_ERROR) {
        msg_error(MSG_ERR_CHAIN, str_cstr(&c->e->name));
    }
    str_free(&drop);
    str_free(&a);
    return rc;
}

/* crown --accept / --reject [<url>] <path> (REQ-F-030). */
static int crown_decide(Crown_t *c, const char *arg, int accept)
{
    const char *label = str_cstr(&c->e->path);
    const AuthChange_t *ch = NULL;
    const char *tab;
    Str_t url = STR_INIT;
    Str_t cwd = STR_INIT;
    Str_t how = STR_INIT;
    Str_t prov = STR_INIT;
    Str_t info = STR_INIT;
    size_t k;
    size_t n = 0U;
    int ok;
    int rc = GS_EXIT_ERROR;

    /* --accept anche un cambio gia' rifiutato (si torna indietro);
     * --reject solo uno in attesa. */
    for (k = 0U; k < c->rv->nchanges; k++) {
        if ((accept != 0) || (c->rv->changes[k].rejected == 0U)) {
            if (ch == NULL) {
                ch = &c->rv->changes[k];
            }
            n++;
        }
    }
    if ((n == 0U) && ((arg == NULL) || (accept == 0))) {
        msg_error(MSG_ERR_NO_AUTH_CHANGE, label);
        goto exit;
    }
    if (arg != NULL) {
        str_addf(&cwd, "%s/%s", str_cstr(&c->r->top), str_cstr(&c->r->prefix));
        if (remote_arg_url(str_cstr(&c->r->top), str_cstr(&cwd), arg, &url) != GS_OK) {
            str_reset(&url);
            str_add(&url, arg);
        }
        ch = NULL;
        for (k = 0U; (k < c->rv->nchanges) && (ch == NULL); k++) {
            if (((accept != 0) || (c->rv->changes[k].rejected == 0U)) &&
                remote_same(str_cstr(&c->rv->changes[k].authority), str_cstr(&url))) {
                ch = &c->rv->changes[k];
            }
        }
        if ((ch == NULL) && (accept != 0) && rejected_had(c->e, str_cstr(&url))) {
            rc = revive(c, str_cstr(&url));
            goto exit;
        }
        if (ch == NULL) {
            msg_error(MSG_ERR_AUTH_CHANGE_UNKNOWN, label, arg);
            goto exit;
        }
    } else if (n > 1U) {
        str_addf(&how, "%s %s", str_cstr(&ch->authority), label);
        msg_error(MSG_ERR_AUTH_CHANGES_URL, label, (unsigned)n,
                  accept ? "--accept" : "--reject", str_cstr(&how));
        rc = GS_EXIT_USAGE;
        goto exit;
    } else {
        /* l'unico */
    }
    if (accept != 0) {
        cmd_auth_describe(ch, &prov, &info);
        printf(msg(MSG_CROWN_ACCEPT_NOTICE), label, str_cstr(&c->rv->authority),
               str_cstr(&ch->authority), str_cstr(&prov), str_cstr(&info));
        putchar('\n');
        tab = strchr(str_cstr(&ch->key), '\t');
        if ((ch->creator != 0U) && (tab != NULL)) {
            printf("    ");
            printf(msg(MSG_AUTH_NEW_CREATOR), tab + 1);
            putchar('\n');
        }
        notice_peers(c);
        ok = confirm(c->yes, label);
        if (ok <= 0) {
            rc = refused(ok);
            goto exit;
        }
    }
    str_reset(&url);
    str_add(&url, str_cstr(&ch->authority)); /* ch non vale dopo la ricezione */
    str_reset(&how);
    str_add(&how, str_cstr(&ch->key));
    if (record(c->e, accept, str_cstr(&how)) != GS_OK) {
        msg_error(MSG_ERR_CHAIN, str_cstr(&c->e->name));
        goto exit;
    }
    printf(msg(accept ? MSG_CROWN_ACCEPTED : MSG_CROWN_REJECTED), label, str_cstr(&url));
    putchar('\n');
    rc = receive_again(c, 0); /* T avanza, o i suoi avvisi si cancellano */

exit:
    str_free(&info);
    str_free(&prov);
    str_free(&how);
    str_free(&cwd);
    str_free(&url);
    return rc;
}

/*
 * Un indirizzo della linea di successione (D-114): 1 se dichiara
 * un'autorita' nuova (esito in out; con apply e un erede, seguito), 0 se
 * non risponde o non dice nulla di nuovo: un'altra famiglia, una versione
 * che T contiene gia', la stessa autorita' di T, un'autorita' rifiutata.
 */
static int visit_one(Crown_t *c, const char *url, const Auth_t *ta,
                     const Heirs_t *h, const Str_t *order, int apply,
                     HeirVisit_t *out)
{
    const char *label = str_cstr(&c->e->path);
    Auth_t au = AUTH_INIT;
    Str_t old = STR_INIT;
    const char *to;
    Oid_t u;
    int found = 0;

    fetch_tip(c, url, &u);
    if ((c->batch_ok == 0) || (u.len == 0U) ||
        (auth_of(c->b, &u, &au, NULL) != GS_OK) || !auth_creator_same(ta, &au) ||
        merge_is_ancestor(&u, &c->t)) {
        goto exit;
    }
    to = auth_authority(&au);
    if (remote_same(to, auth_authority(ta)) || rejected_had(c->e, to)) {
        goto exit;
    }
    found = 1;
    str_add(&out->where, url);
    str_add(&out->to, to);
    out->pos = heirs_position(order, to);
    if (!heirs_find(h, to, NULL)) {
        out->kind = HEIR_VISIT_OTHER; /* non un erede: lo decide l'utente */
        goto exit;
    }
    out->kind = HEIR_VISIT_FOUND;
    if (apply != 0) {
        str_add(&old, auth_authority(ta));
        printf(msg(MSG_PULL_HEIR_FOLLOWED), label, out->pos, to, str_cstr(&old));
        putchar('\n');
        out->kind = (follow(c, url, &u, &au, 0) == GS_EXIT_OK) ? HEIR_VISIT_FOLLOWED
                                                               : HEIR_VISIT_ERROR;
    }

exit:
    drop_tmp(c);
    str_free(&old);
    auth_free(&au);
    return found;
}

void heirvisit_free(HeirVisit_t *v)
{
    str_free(&v->where);
    str_free(&v->to);
}

int cmd_heirs_visit(Repo_t *r, SharedEntry_t *e, GitBatch_t *b, int apply,
                    HeirVisit_t *out)
{
    Received_t rv = RECEIVED_INIT;
    Auth_t ta = AUTH_INIT;
    Auth_t ac = AUTH_INIT;
    Heirs_t h = HEIRS_INIT;
    Str_t order = STR_INIT;
    Str_t ref = STR_INIT;
    Crown_t c;
    const char *url;
    Oid_t uc;

    str_reset(&out->where);
    str_reset(&out->to);
    out->kind = HEIR_VISIT_NONE;
    out->pos = 0U;
    c.r = r;
    c.e = e;
    c.b = b;
    c.rv = &rv;
    c.yes = 1;
    c.quiet = 1;
    c.batch_ok = 1;
    chain_ref(&ref, e->id);
    if ((receive_entry(r, e, b, 0, &rv) != GS_OK) ||
        (git_batch_query(b, str_cstr(&ref), &c.t) != GIT_OBJ_COMMIT) ||
        (heirs_of(b, &c.t, &ta, NULL, &h) != GS_OK) || (ta.creator.len == 0U)) {
        out->kind = HEIR_VISIT_ERROR;
        goto exit;
    }
    heirs_order(&ta, &h, &order);
    for (url = str_znext(&order, NULL); url != NULL; url = str_znext(&order, url)) {
        if (visit_one(&c, url, &ta, &h, &order, apply, out)) {
            goto exit;
        }
        if (c.batch_ok == 0) {
            out->kind = HEIR_VISIT_ERROR;
            goto exit;
        }
    }
    /* Nessun erede: il creatore, se l'autorita' e' un sovrano. Tornato, non
     * si segue da solo: decidono le persone (crown --restore). */
    url = str_cstr(&ta.creator);
    if (!remote_same(url, auth_authority(&ta))) {
        fetch_tip(&c, url, &uc);
        if ((c.batch_ok != 0) && (uc.len > 0U) &&
            (auth_of(b, &uc, &ac, NULL) == GS_OK) && auth_creator_same(&ta, &ac) &&
            !remote_same(auth_authority(&ac), auth_authority(&ta))) {
            out->kind = HEIR_VISIT_CREATOR;
            str_add(&out->where, url);
        }
        drop_tmp(&c);
    }

exit:
    if (c.batch_ok == 0) {
        out->kind = HEIR_VISIT_ERROR;
    }
    str_free(&ref);
    str_free(&order);
    heirs_free(&h);
    auth_free(&ac);
    auth_free(&ta);
    received_free(&rv);
    return c.batch_ok;
}

int cmd_creator_back(Repo_t *r, SharedEntry_t *e, GitBatch_t *b, int *back)
{
    Received_t rv = RECEIVED_INIT;
    Auth_t ta = AUTH_INIT;
    Auth_t ac = AUTH_INIT;
    Str_t ref = STR_INIT;
    Crown_t c;
    const char *creator;
    Oid_t uc;

    *back = 0;
    c.r = r;
    c.e = e;
    c.b = b;
    c.rv = &rv;
    c.yes = 1;
    c.quiet = 1;
    c.batch_ok = 1;
    chain_ref(&ref, e->id);
    if ((git_batch_query(b, str_cstr(&ref), &c.t) != GIT_OBJ_COMMIT) ||
        (auth_of(b, &c.t, &ta, NULL) != GS_OK) || (ta.creator.len == 0U)) {
        goto exit;
    }
    creator = str_cstr(&ta.creator);
    if (remote_same(creator, auth_authority(&ta))) {
        goto exit; /* l'autorita' e' il creatore */
    }
    fetch_tip(&c, creator, &uc);
    /* Tornato, e la sua linea non porta all'autorita' di T. */
    if ((c.batch_ok != 0) && (uc.len > 0U) &&
        (auth_of(b, &uc, &ac, NULL) == GS_OK) && auth_creator_same(&ta, &ac) &&
        !remote_same(auth_authority(&ac), auth_authority(&ta))) {
        *back = 1;
    }
    drop_tmp(&c);

exit:
    str_free(&ref);
    auth_free(&ac);
    auth_free(&ta);
    received_free(&rv);
    return c.batch_ok;
}

int cmd_crown(int argc, char **argv)
{
    Repo_t repo = REPO_INIT;
    GitBatch_t batch;
    Received_t rv = RECEIVED_INIT;
    Crown_t c;
    Str_t ref = STR_INIT;
    unsigned char *sel = NULL;
    const char *pos[2] = { NULL, NULL };
    const char *path;
    eCrownMode_t mode = CROWN_URL;
    size_t i;

    c.r = NULL;
    int npos = 0;
    int nmode = 0;
    int yes = 0;
    int opts = 1;
    int k;
    int opened = 0;
    int rc = GS_EXIT_USAGE;
    Oid_t m;

    for (k = 1; k < argc; k++) {
        if (opts && (strcmp(argv[k], "--") == 0)) {
            opts = 0;
        } else if (opts && (strcmp(argv[k], "--yes") == 0)) {
            yes = 1;
        } else if (opts && (strcmp(argv[k], "--restore") == 0)) {
            mode = CROWN_RESTORE;
            nmode++;
        } else if (opts && (strcmp(argv[k], "--accept") == 0)) {
            mode = CROWN_ACCEPT;
            nmode++;
        } else if (opts && (strcmp(argv[k], "--reject") == 0)) {
            mode = CROWN_REJECT;
            nmode++;
        } else if (opts && (argv[k][0] == '-') && (argv[k][1] != '\0')) {
            msg_error(MSG_ERR_UNKNOWN_OPT, argv[k]);
            fputs(msg(MSG_USAGE_CROWN), stderr);
            goto exit;
        } else if (npos < 2) {
            pos[npos++] = argv[k];
        } else {
            npos++;
        }
    }
    /* [<remote|url>] <path>; --restore <path>; --accept/--reject [<url>]
     * <path>. Senza <remote|url> vale origin (D-114). */
    if ((nmode > 1) || (npos == 0) || (npos > 2) ||
        ((mode == CROWN_RESTORE) && (npos != 1))) {
        msg_error((npos > 2) ? MSG_ERR_TOO_MANY_ARGS : MSG_ERR_CROWN_ARGS);
        fputs(msg(MSG_USAGE_CROWN), stderr);
        goto exit;
    }
    path = pos[npos - 1];
    rc = cmd_open(&repo, &batch);
    if (rc != GS_EXIT_OK) {
        goto exit;
    }
    opened = 1;
    sel = xmalloc((repo.n > 0U) ? repo.n : 1U);
    if (!cmd_select(&repo, path, sel)) {
        rc = GS_EXIT_ERROR;
        goto exit;
    }
    i = 0U;
    while ((i < repo.n) && (sel[i] == 0U)) {
        i++;
    }
    if (i == repo.n) {
        rc = GS_EXIT_ERROR; /* non succede: cmd_select l'ha detto */
        goto exit;
    }
    c.batch_ok = 1;
    c.r = &repo;
    c.e = &repo.v[i];
    c.b = &batch;
    c.rv = &rv;
    c.yes = yes;
    c.quiet = 0;
    rc = GS_EXIT_ERROR;
    /* Prima la ricezione locale: T, l'autorita' e i cambi in attesa. */
    if (receive_entry(&repo, c.e, &batch, 0, &rv) != GS_OK) {
        msg_error(MSG_ERR_CHAIN, str_cstr(&c.e->name));
        goto exit;
    }
    chain_ref(&ref, c.e->id);
    if (git_batch_query(&batch, str_cstr(&ref), &c.t) != GIT_OBJ_COMMIT) {
        msg_error(MSG_ERR_CHAIN, str_cstr(&c.e->name));
        goto exit;
    }
    if (rv.creator.len == 0U) {
        msg_error(MSG_ERR_CROWN_NO_CREATOR, str_cstr(&c.e->path));
        goto exit;
    }
    if ((mode == CROWN_ACCEPT) || (mode == CROWN_REJECT)) {
        rc = crown_decide(&c, (npos == 2) ? pos[0] : NULL, mode == CROWN_ACCEPT);
        goto exit;
    }
    merge_ref(&ref, c.e->id);
    if (git_batch_query(&batch, str_cstr(&ref), &m) == GIT_OBJ_COMMIT) {
        msg_error(MSG_ERR_CROWN_MERGING, str_cstr(&c.e->path));
        goto exit;
    }
    for (i = 0U; i < rv.nchanges; i++) {
        if (rv.changes[i].rejected == 0U) {
            cmd_auth_how(c.e, &rv, i, &ref);
            msg_error(MSG_WARN_AUTH_PENDING, str_cstr(&c.e->path),
                      str_cstr(&rv.changes[i].authority), str_cstr(&ref));
            goto exit; /* prima si decide quello (D-111) */
        }
    }
    if ((mode == CROWN_URL) && (npos == 1)) {
        /* la propria repo remota (D-114) */
        if (remote_url(str_cstr(&repo.top), "origin", &ref) != GS_OK) {
            msg_error(MSG_ERR_NO_REMOTE, "origin");
            goto exit;
        }
        pos[0] = "origin";
    }
    rc = (mode == CROWN_RESTORE) ? crown_restore(&c) : crown_url(&c, pos[0]);

exit:
    free(sel);
    if (opened != 0) {
        if ((c.r == NULL) || (c.batch_ok != 0)) {
            git_batch_close(&batch);
        }
        repo_close(&repo);
    }
    received_free(&rv);
    str_free(&ref);
    return rc;
}
