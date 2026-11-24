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
 * Il primo argomento e' un <remote> se la repo ha un remote con quel nome,
 * come in "git push <repository>"; "--" lo fa leggere come path.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chain.h"
#include "cmd.h"
#include "cmd_common.h"
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
    Oid_t blob;
    Oid_t creator;
    Oid_t v;
    int rc = GS_EXIT_ERROR;

    oid_to_hex(t, hex);
    str_addf(&ref, "%s:content", hex);
    if (!git_batch_blob(b, str_cstr(&ref), &blob) ||
        (remote_url_blob(str_cstr(url), &creator) != GS_OK) ||
        (chain_commit(&blob, t, NULL, &creator, str_cstr(&e->name), &v) != GS_OK)) {
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
        if (remote_find_own(b, str_cstr(url), e->id, t, &creator, &v)) {
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

/* Copia delle catene nel proprio remote: per chi clona questa repo. Si
 * salta il file il cui creatore e' il proprio remote. */
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
 * Versioni ufficiali: un push per creatore, con le catene il cui T non e'
 * gia' l'ultima versione vista. GS_EXIT_ERROR se il creatore ne rifiuta
 * qualcuna.
 */
static int push_creators(const Repo_t *r, const unsigned char *sel,
                         const Str_t *urls, const Oid_t *tips, const Oid_t *seen)
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
    size_t i;
    size_t j;
    size_t k;
    size_t m;
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
                printf(msg(MSG_PUSH_ON_CREATOR), str_cstr(&r->v[j].path), url);
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
            e = &r->v[idx[k]];
            label = str_cstr(&e->path);
            switch ((eRemotePush_t)res[k]) {
            case REMOTE_PUSH_OK:
                remote_seen_ref(&ref, e->id);
                if (git_set_ref(str_cstr(&ref), &v[k], "git shared: push") == GS_OK) {
                    (void)remote_seen_write(e->id, url, NULL, NULL);
                }
                printf(msg(MSG_PUSH_TO_CREATOR), label, url);
                putchar('\n');
                break;
            case REMOTE_PUSH_NEWER:
                msg_error(MSG_ERR_CREATOR_NEWER, label, url);
                rc = GS_EXIT_ERROR;
                break;
            default:
                msg_error(MSG_ERR_CREATOR_REFUSED, label, url);
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
    Str_t *urls = NULL;    /* creatore di ogni voce, "" = nessuno */
    Oid_t *tips = NULL;    /* T di ogni voce */
    Oid_t *seen = NULL;    /* ultima versione vista del creatore */
    Str_t ref = STR_INIT;
    const SharedEntry_t *e;
    const char *remote = NULL;
    const char *path = NULL;
    const char *label;
    char **pos;
    size_t n = 0U;
    size_t i;
    int npos;
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
    tips = xmalloc(((n > 0U) ? n : 1U) * sizeof *tips);
    seen = xmalloc(((n > 0U) ? n : 1U) * sizeof *seen);
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
        if (remote_creator_url(&batch, &tips[i], &urls[i]) != GS_OK) {
            str_reset(&urls[i]);
        }
        /* L'ultima versione vista conta solo se e' stata vista su questo
         * creatore (D-112): altrimenti push non sa che cosa ha. */
        remote_seen_ref(&ref, e->id);
        if ((git_batch_query(&batch, str_cstr(&ref), &seen[i]) != GIT_OBJ_COMMIT) ||
            (e->seen_url.len == 0U) || (urls[i].len == 0U) ||
            !remote_same(str_cstr(&e->seen_url), str_cstr(&urls[i]))) {
            seen[i].len = 0U;
        }
        if (urls[i].len > 0U) {
            any = 1;
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
    push_copies(&repo, sel, urls, tips, &own);
    if (push_creators(&repo, sel, urls, tips, seen) != GS_EXIT_OK) {
        rc = GS_EXIT_ERROR;
    }

exit:
    if (urls != NULL) {
        for (i = 0U; i < n; i++) {
            str_free(&urls[i]);
        }
    }
    free(seen);
    free(tips);
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
