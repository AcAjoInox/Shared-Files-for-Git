/*
 * cmd_push.c - "git shared push [<remote>] [--] [<path>]"
 * (REQ-F-025, D-104, D-110).
 *
 * Manda al creatore le versioni gia' pubblicate: non ne crea, salvo la
 * prima con il creatore. Come git push non committa, push non pubblica:
 * prima publish, poi push. Per ogni file scelto:
 *   - senza creatore, con <remote> e <path>: la repo remota diventa il
 *     creatore. Nasce una versione con lo stesso contenuto di T e la voce
 *     "creator" (D-103), che va prima nella repo remota e solo dopo, se il
 *     push riesce, nella catena locale; poi l'avviso ai peer locali, che la
 *     ricevono come ogni altra;
 *   - con creatore: T in copia nel proprio remote (<remote>, altrimenti
 *     origin: per chi clona questa repo), poi nel creatore (la versione
 *     ufficiale), saltato se l'ultima versione vista e' gia' T. Un push per
 *     repo, con tutte le catene. Il creatore accetta solo un fast-forward e
 *     solo da chi ha i permessi: se rifiuta, la versione resta qui come
 *     proposta (ideeRemote §4.7).
 * Riuscito il push al creatore, l'ultima versione vista diventa T.
 *
 * Eredi (D-114, heirs.h): se il proprio remote puo' diventare erede
 * dell'autorita' (c'e' posto, un'altra casata, shared.heir non e' false),
 * al posto di T va una versione con lo stesso contenuto e il proprio
 * remote tra gli eredi. Come il primo push va prima sull'autorita' e solo
 * dopo, se riesce, nella catena locale: rifiutata non lascia nulla. Se non
 * c'era altro da mandare, il rifiuto non e' un errore: un lettore resta
 * "already on". La copia sul proprio remote viene dopo l'autorita', con
 * la versione che e' rimasta. Se l'autorita' non risponde e il proprio
 * remote e' un erede, una nota dice come prenderne il posto.
 *
 * Il primo argomento e' un <remote> se la repo ha un remote con quel nome,
 * come in "git push <repository>"; "--" lo fa leggere come path.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chain.h"
#include "cmd.h"
#include "cmd_common.h"
#include "heirs.h"
#include "mem.h"
#include "receive.h"
#include "remote.h"

/*
 * Primo push (D-104, D-110): la versione con il creatore va prima nella
 * repo remota, poi nella catena locale. Se il push non riesce non cambia
 * nulla: la repo remota non e' diventata il creatore. Contenuto: quello di
 * T, la versione gia' pubblicata.
 */
static int become_creator(const Repo_t *r, const SharedEntry_t *e,
                          GitBatch_t *b, const Oid_t *t, const Str_t *url)
{
    const char *label = str_cstr(&e->path);
    char hex[OID_HEX_SIZE];
    Str_t ref = STR_INIT;
    AuthBlobs_t auth;
    Oid_t blob;
    Oid_t v;
    int rc = GS_EXIT_ERROR;

    oid_to_hex(t, hex);
    str_addf(&ref, "%s:content", hex);
    auth.sovereigns.len = 0U; /* un creatore, mai incoronato */
    auth.heirs.len = 0U;      /* e senza eredi */
    if (!git_batch_blob(b, str_cstr(&ref), &blob) ||
        (remote_url_blob(str_cstr(url), &auth.creator) != GS_OK) ||
        (chain_commit(&blob, t, NULL, &auth, str_cstr(&e->name), &v) != GS_OK)) {
        msg_error(MSG_ERR_PUBLISH, label);
        goto exit;
    }
    switch (remote_push(str_cstr(url), e->id, &v)) {
    case REMOTE_PUSH_OK:
        break;
    case REMOTE_PUSH_NEWER:
        /* P-03 (D-112): un primo push riuscito a cui e' mancato
         * l'update-ref ha lasciato la versione sul remoto: si riconosce e
         * si prende quella, invece di ricrearla con un'altra data. */
        if (remote_find_own(b, str_cstr(url), e->id, t, NULL, &auth, &v)) {
            break;
        }
        msg_error(MSG_ERR_CREATOR_PUSH, label, str_cstr(url));
        goto exit;
    default:
        msg_error(MSG_ERR_CREATOR_PUSH, label, str_cstr(url));
        goto exit;
    }
    /* Condizionata a T. Se fallisce (T cambiata tra la lettura e qui) la
     * versione e' nella repo remota ma non nella catena locale: caso raro,
     * noto del prototipo (ideeRemote §9, P-03). */
    chain_ref(&ref, e->id);
    if (git_update_ref(str_cstr(&ref), &v, t, "git shared: push") != GS_OK) {
        msg_error(MSG_ERR_PUBLISH, label);
        goto exit;
    }
    remote_seen_ref(&ref, e->id);
    if (git_set_ref(str_cstr(&ref), &v, "git shared: push") == GS_OK) {
        (void)remote_seen_write(e->id, str_cstr(url), NULL, NULL);
    }
    printf(msg(MSG_PUSH_CREATOR_NEW), label, str_cstr(url));
    putchar('\n');
    rc = cmd_publish_entry(r, e, b, NULL, &v); /* avviso ai peer locali */

exit:
    str_free(&ref);
    return rc;
}

/* shared.heir = false: il proprio remote non diventa mai erede (D-114).
 * Un processo, letto una volta e solo se serve. */
static int heir_allowed(void)
{
    static const char *const args[] = { "config", "--bool", "--get", "shared.heir",
                                        NULL };
    static int cached = -1;
    Str_t out = STR_INIT;

    if (cached < 0) {
        cached = 1;
        if (git_run(args, PROC_STDERR_NULL, &out) == GS_OK) {
            str_trim_eol(&out);
            cached = !str_eq(&out, "false");
        }
        str_free(&out);
    }
    return cached;
}

/*
 * Iscrizione (D-114): se own puo' diventare erede dell'autorita' di T, la
 * versione che lo dice in *v (genitore T, stesso contenuto, la riga nuova
 * in heirs), le sue voci in *ab e la posizione in *pos. Non tocca nessuna
 * ref. 0 se non c'e' nulla da fare.
 */
static int enroll_prepare(GitBatch_t *b, const SharedEntry_t *e, const Oid_t *t,
                          const char *own, Oid_t *v, AuthBlobs_t *ab, unsigned *pos)
{
    char hex[OID_HEX_SIZE];
    Auth_t a = AUTH_INIT;
    Heirs_t h = HEIRS_INIT;
    Str_t ref = STR_INIT;
    Oid_t blob;
    int ok = 0;

    *pos = 0U;
    if ((*own == '\0') || (heirs_of(b, t, &a, ab, &h) != GS_OK)) {
        goto exit;
    }
    *pos = heirs_can_join(&a, &h, own);
    if ((*pos == 0U) || !heir_allowed()) {
        goto exit;
    }
    heirs_add(&h, auth_authority(&a), own);
    oid_to_hex(t, hex);
    str_addf(&ref, "%s:content", hex);
    ok = git_batch_blob(b, str_cstr(&ref), &blob) &&
         (heirs_write(&h, &ab->heirs) == GS_OK) &&
         (chain_commit(&blob, t, NULL, ab, str_cstr(&e->name), v) == GS_OK);

exit:
    str_free(&ref);
    heirs_free(&h);
    auth_free(&a);
    return ok;
}

/*
 * Autorita' che non risponde (D-114): se il proprio remote e' un erede di
 * T, la nota che dice come prenderne il posto. Un ls-remote, solo qui.
 */
static void heir_note(GitBatch_t *b, const SharedEntry_t *e, const Oid_t *t,
                      const char *own, const char *url, int sov)
{
    Auth_t a = AUTH_INIT;
    Heirs_t h = HEIRS_INIT;
    Str_t order = STR_INIT;
    unsigned pos;

    if ((*own != '\0') && (heirs_of(b, t, &a, NULL, &h) == GS_OK) && (h.n > 0U)) {
        heirs_order(&a, &h, &order);
        pos = heirs_position(&order, own);
        if ((pos > 0U) && !remote_reachable(url)) {
            printf(msg(MSG_PUSH_HEIR_NOTE), str_cstr(&e->path), auth_role(sov), url,
                   pos, str_cstr(&e->path));
            putchar('\n');
        }
    }
    str_free(&order);
    heirs_free(&h);
    auth_free(&a);
}

/* L'iscrizione accettata dall'autorita': la versione entra nella catena
 * locale (condizionata a T) e ai peer locali. */
static int enroll_done(const Repo_t *r, const SharedEntry_t *e, GitBatch_t *b,
                       const Oid_t *t, Oid_t *v, const char *own, unsigned pos,
                       const char *url)
{
    const char *label = str_cstr(&e->path);
    Str_t ref = STR_INIT;
    int rc = GS_EXIT_ERROR;

    chain_ref(&ref, e->id);
    if (git_update_ref(str_cstr(&ref), v, t, "git shared: push") != GS_OK) {
        msg_error(MSG_ERR_CHAIN, str_cstr(&e->name)); /* P-03: il prossimo push */
        goto exit;
    }
    printf(msg(MSG_PUSH_HEIR_NEW), label, own, pos, url);
    putchar('\n');
    rc = cmd_publish_entry(r, e, b, NULL, v);

exit:
    str_free(&ref);
    return rc;
}

/* Copia delle catene nel proprio remote: per chi clona questa repo. Si
 * salta il file la cui autorita' e' il proprio remote (D-111: non il
 * creatore, altrimenti dopo un passaggio di consegne si salterebbe proprio
 * la copia che informa i lettori). */
static void push_copies(const Repo_t *r, const unsigned char *sel,
                        const Str_t *urls, const Oid_t *tips, const Str_t *own)
{
    size_t n = r->n;
    Oid_t *v = xmalloc(((n > 0U) ? n : 1U) * sizeof *v);
    size_t *idx = xmalloc(((n > 0U) ? n : 1U) * sizeof *idx);
    uint8_t *res = xmalloc((n > 0U) ? n : 1U);
    Str_t ids = STR_INIT;
    const char *label;
    size_t i;
    size_t m = 0U;

    for (i = 0U; (i < n) && (own->len > 0U); i++) {
        if ((sel[i] != 0U) && !remote_same(str_cstr(&urls[i]), str_cstr(own))) {
            str_addz(&ids, r->v[i].id);
            v[m] = tips[i];
            idx[m++] = i;
        }
    }
    if (m > 0U) {
        remote_push_many(str_cstr(own), &ids, v, res);
    }
    for (i = 0U; i < m; i++) {
        label = str_cstr(&r->v[idx[i]].path);
        if (res[i] == (uint8_t)REMOTE_PUSH_OK) {
            printf(msg(MSG_PUB_COPIED), label, str_cstr(own));
            putchar('\n');
        } else {
            msg_warning(MSG_WARN_COPY_FAILED, label, str_cstr(own));
        }
    }
    str_free(&ids);
    free(res);
    free(idx);
    free(v);
}

/*
 * Push rifiutato perche' l'autorita' e' piu' avanti: se la sua versione,
 * gia' qui, porta un cambio di autorita' da decidere, pull non servirebbe
 * (riporterebbe lo stesso cambio) e si dice di decidere (D-111). 1 se e'
 * cosi', con l'argomento di crown --accept in how.
 */
static int pending_on_authority(const Repo_t *r, const SharedEntry_t *e,
                                GitBatch_t *b, Str_t *how)
{
    Received_t rv = RECEIVED_INIT;
    size_t k;
    int found = 0;

    if (receive_pending_auth(r, e, b, &rv) == GS_OK) {
        for (k = 0U; (k < rv.nchanges) && (found == 0); k++) {
            if ((rv.changes[k].by_authority != 0U) && (rv.changes[k].rejected == 0U)) {
                cmd_auth_how(e, &rv, k, how);
                found = 1;
            }
        }
    }
    received_free(&rv);
    return found;
}

/* Iscrizioni in corso (D-114), per voce: la versione d'iscrizione e' in
 * tips[i], T in orig[i]. */
typedef struct {
    unsigned char *on;  /* 1 se la voce prova a iscriversi */
    Oid_t *orig;        /* T */
    AuthBlobs_t *ab;    /* voci della versione d'iscrizione */
    unsigned *pos;      /* posizione che prenderebbe */
    const char *own;    /* il proprio remote */
} Enroll_t;

/*
 * Versioni ufficiali: un push per autorita', con le catene il cui T non e'
 * gia' l'ultima versione vista. GS_EXIT_ERROR se l'autorita' ne rifiuta
 * qualcuna. sov[i]: l'autorita' e' un sovrano (per i messaggi). Le
 * iscrizioni (en) finiscono qui: tips[i] torna T se non sono passate.
 */
static int push_creators(const Repo_t *r, GitBatch_t *b, const unsigned char *sel,
                         const Str_t *urls, const unsigned char *sov,
                         Oid_t *tips, const Oid_t *seen, const Enroll_t *en)
{
    size_t n = r->n;
    Oid_t *v = xmalloc(((n > 0U) ? n : 1U) * sizeof *v);
    size_t *idx = xmalloc(((n > 0U) ? n : 1U) * sizeof *idx);
    uint8_t *res = xmalloc((n > 0U) ? n : 1U);
    uint8_t *done = xmalloc((n > 0U) ? n : 1U);
    Str_t ids = STR_INIT;
    Str_t ref = STR_INIT;
    const SharedEntry_t *e;
    const char *url;
    const char *label;
    Oid_t own_v;
    size_t i;
    size_t j;
    size_t k;
    size_t m;
    size_t x;
    int rc = GS_EXIT_OK;

    memset(done, 0, (n > 0U) ? n : 1U);
    for (i = 0U; i < n; i++) {
        if ((sel[i] == 0U) || (done[i] != 0U)) {
            continue;
        }
        url = str_cstr(&urls[i]);
        str_reset(&ids);
        m = 0U;
        for (j = i; j < n; j++) {
            if ((sel[j] == 0U) || (done[j] != 0U) ||
                !remote_same(str_cstr(&urls[j]), url)) {
                continue;
            }
            done[j] = 1U;
            if (oid_eq(&seen[j], &tips[j])) {
                printf(msg(MSG_PUSH_ON_CREATOR), str_cstr(&r->v[j].path),
                       auth_role(sov[j]), url);
                putchar('\n');
            } else {
                str_addz(&ids, r->v[j].id);
                v[m] = tips[j];
                idx[m++] = j;
            }
        }
        if (m > 0U) {
            remote_push_many(url, &ids, v, res);
        }
        for (k = 0U; k < m; k++) {
            x = idx[k];
            e = &r->v[x];
            label = str_cstr(&e->path);
            /* P-03 (D-112): un'iscrizione riuscita a cui e' mancato
             * l'update-ref ha lasciato la versione sull'autorita'. */
            if ((en->on[x] != 0U) && (res[k] == (uint8_t)REMOTE_PUSH_NEWER) &&
                remote_find_own(b, url, e->id, &en->orig[x], NULL, &en->ab[x], &own_v)) {
                v[k] = own_v;
                tips[x] = own_v;
                res[k] = (uint8_t)REMOTE_PUSH_OK;
            }
            if ((en->on[x] != 0U) && (res[k] != (uint8_t)REMOTE_PUSH_OK)) {
                tips[x] = en->orig[x]; /* l'iscrizione non e' passata */
                if (oid_eq(&seen[x], &en->orig[x])) {
                    /* non c'era altro da mandare: come prima */
                    printf(msg(MSG_PUSH_ON_CREATOR), label, auth_role(sov[x]), url);
                    putchar('\n');
                    if (res[k] == (uint8_t)REMOTE_PUSH_REFUSED) {
                        heir_note(b, e, &en->orig[x], en->own, url, sov[x]);
                    }
                    continue;
                }
            }
            switch ((eRemotePush_t)res[k]) {
            case REMOTE_PUSH_OK:
                remote_seen_ref(&ref, e->id);
                if (git_set_ref(str_cstr(&ref), &v[k], "git shared: push") == GS_OK) {
                    (void)remote_seen_write(e->id, url, NULL, NULL);
                }
                printf(msg(MSG_PUSH_TO_CREATOR), label, auth_role(sov[x]), url);
                putchar('\n');
                if ((en->on[x] != 0U) &&
                    (enroll_done(r, e, b, &en->orig[x], &v[k], en->own, en->pos[x],
                                 url) != GS_EXIT_OK)) {
                    rc = GS_EXIT_ERROR;
                }
                break;
            case REMOTE_PUSH_NEWER:
                if (pending_on_authority(r, e, b, &ref)) {
                    msg_error(MSG_ERR_AUTH_DECIDE_FIRST, label, url, str_cstr(&ref));
                } else {
                    msg_error(MSG_ERR_CREATOR_NEWER, label, auth_role(sov[x]), url);
                }
                rc = GS_EXIT_ERROR;
                break;
            default:
                msg_error(MSG_ERR_CREATOR_REFUSED, label, auth_role(sov[x]), url);
                heir_note(b, e, &tips[x], en->own, url, sov[x]);
                rc = GS_EXIT_ERROR;
                break;
            }
        }
    }
    str_free(&ref);
    str_free(&ids);
    free(done);
    free(res);
    free(idx);
    free(v);
    return rc;
}

int cmd_push(int argc, char **argv)
{
    CmdOpts_t o;
    Repo_t repo = REPO_INIT;
    GitBatch_t batch;
    unsigned char *sel = NULL;
    Str_t own = STR_INIT;  /* URL del proprio remote, "" = nessuno */
    Str_t *urls = NULL;    /* autorita' di ogni voce, "" = nessuna */
    unsigned char *sov = NULL; /* l'autorita' e' un sovrano */
    Oid_t *tips = NULL;    /* T di ogni voce */
    Oid_t *seen = NULL;    /* ultima versione vista del creatore */
    Enroll_t en = { NULL, NULL, NULL, NULL, "" };
    Str_t ref = STR_INIT;
    const SharedEntry_t *e;
    const char *remote = NULL;
    const char *path = NULL;
    const char *label;
    char **pos;
    Oid_t ref_v;
    size_t n = 0U;
    size_t i;
    int npos;
    int sovereign;
    int first;
    int any = 0;
    int opened = 0;
    int rc = GS_EXIT_USAGE;

    first = cmd_parse(argc, argv, 0U, MSG_USAGE_PUSH, &o);
    if (first < 0) {
        goto exit;
    }
    pos = argv + first;
    npos = argc - first;
    /* <remote> -- <path>, oppure <remote> <path>: il primo e' il remote. */
    if ((o.dashdash == 0U) && (npos >= 2) && (strcmp(pos[1], "--") == 0)) {
        remote = pos[0];
        pos += 2;
        npos -= 2;
    } else if ((o.dashdash == 0U) && (npos == 2)) {
        remote = pos[0];
        pos++;
        npos--;
    } else {
        /* un solo argomento: si decide sotto, con la repo aperta */
    }
    if (npos > 1) {
        msg_error(MSG_ERR_TOO_MANY_ARGS);
        fputs(msg(MSG_USAGE_PUSH), stderr);
        goto exit;
    }
    rc = cmd_open(&repo, &batch);
    if (rc != GS_EXIT_OK) {
        goto exit;
    }
    opened = 1;
    if ((remote == NULL) && (o.dashdash == 0U) && (npos == 1) &&
        (remote_url(str_cstr(&repo.top), pos[0], &own) == GS_OK)) {
        remote = pos[0]; /* un remote con quel nome: non e' un path */
        npos = 0;
    } else if (remote != NULL) {
        if (remote_url(str_cstr(&repo.top), remote, &own) != GS_OK) {
            msg_error(MSG_ERR_NO_REMOTE, remote);
            rc = GS_EXIT_ERROR;
            goto exit;
        }
    } else {
        (void)remote_url(str_cstr(&repo.top), "origin", &own);
    }
    path = (npos == 1) ? pos[0] : NULL;

    n = repo.n;
    sel = xmalloc((n > 0U) ? n : 1U);
    urls = xmalloc(((n > 0U) ? n : 1U) * sizeof *urls);
    sov = xmalloc((n > 0U) ? n : 1U);
    tips = xmalloc(((n > 0U) ? n : 1U) * sizeof *tips);
    seen = xmalloc(((n > 0U) ? n : 1U) * sizeof *seen);
    en.on = xmalloc((n > 0U) ? n : 1U);
    en.orig = xmalloc(((n > 0U) ? n : 1U) * sizeof *en.orig);
    en.ab = xmalloc(((n > 0U) ? n : 1U) * sizeof *en.ab);
    en.pos = xmalloc(((n > 0U) ? n : 1U) * sizeof *en.pos);
    en.own = str_cstr(&own);
    memset(en.on, 0, (n > 0U) ? n : 1U);
    for (i = 0U; i < n; i++) {
        str_init(&urls[i]);
    }
    if (!cmd_select(&repo, path, sel)) {
        rc = GS_EXIT_ERROR;
        goto exit;
    }

    /* T, creatore e ultima versione vista di ogni file scelto; i file senza
     * creatore si saltano, salvo il primo push. */
    for (i = 0U; i < n; i++) {
        if (sel[i] == 0U) {
            continue;
        }
        e = &repo.v[i];
        label = str_cstr(&e->path);
        chain_ref(&ref, e->id);
        if ((git_batch_query(&batch, str_cstr(&ref), &tips[i]) != GIT_OBJ_COMMIT) &&
            (receive_restore_chain(e, &batch, &tips[i]) != GS_OK)) { /* B-51 */
            msg_error(MSG_ERR_CHAIN, str_cstr(&e->name));
            rc = GS_EXIT_ERROR;
            sel[i] = 0U;
            continue;
        }
        sovereign = 0;
        if (auth_target(&batch, &tips[i], &urls[i], &sovereign) != GS_OK) {
            str_reset(&urls[i]);
            sovereign = 0;
        }
        sov[i] = (unsigned char)sovereign;
        /* L'ultima versione vista conta solo se e' stata vista su questa
         * autorita' (D-112): altrimenti push non sa che cosa ha. */
        remote_seen_ref(&ref, e->id);
        if ((git_batch_query(&batch, str_cstr(&ref), &seen[i]) != GIT_OBJ_COMMIT) ||
            (e->seen_url.len == 0U) || (urls[i].len == 0U) ||
            !remote_same(str_cstr(&e->seen_url), str_cstr(&urls[i]))) {
            seen[i].len = 0U;
        }
        if (urls[i].len > 0U) {
            any = 1;
            /* Il proprio remote tra gli eredi, se puo' (D-114). */
            if (enroll_prepare(&batch, e, &tips[i], en.own, &ref_v, &en.ab[i],
                               &en.pos[i])) {
                en.on[i] = 1U;
                en.orig[i] = tips[i];
                tips[i] = ref_v;
            }
            continue;
        }
        sel[i] = 0U;
        if (path == NULL) {
            /* senza path nessun file diventa creatore */
        } else if (remote == NULL) {
            msg_error(MSG_ERR_PUSH_NO_CREATOR, label, label);
            rc = GS_EXIT_ERROR;
        } else if (become_creator(&repo, e, &batch, &tips[i], &own) != GS_EXIT_OK) {
            rc = GS_EXIT_ERROR;
        } else {
            any = 1;
        }
    }
    if ((any == 0) && (path == NULL)) {
        msg_error(MSG_ERR_PUSH_NOTHING);
        rc = GS_EXIT_ERROR;
        goto exit;
    }
    /* Prima l'autorita', poi la copia: con la versione che e' rimasta
     * (un'iscrizione rifiutata non va nemmeno nella copia, D-114). */
    if (push_creators(&repo, &batch, sel, urls, sov, tips, seen, &en) != GS_EXIT_OK) {
        rc = GS_EXIT_ERROR;
    }
    push_copies(&repo, sel, urls, tips, &own);

exit:
    if (urls != NULL) {
        for (i = 0U; i < n; i++) {
            str_free(&urls[i]);
        }
    }
    free(en.pos);
    free(en.ab);
    free(en.orig);
    free(en.on);
    free(seen);
    free(tips);
    free(sov);
    free(urls);
    free(sel);
    if (opened != 0) {
        git_batch_close(&batch);
        repo_close(&repo);
    }
    str_free(&ref);
    str_free(&own);
    return rc;
}
