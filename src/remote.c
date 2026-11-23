/*
 * remote.c - la catena in remoto: il creatore.
 */
#include "remote.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bounds.h"
#include "chain.h"
#include "mem.h"

/*
 * Un URL che arriva dalla catena e' scritto da altri: non deve poter
 * diventare un'opzione di git ("--upload-pack=<comando>") ne' spezzare le
 * righe. Si rifiutano un trattino in testa, i caratteri di controllo e gli
 * URL troppo lunghi (REQ-NF-011).
 */
static int url_ok(const char *url)
{
    const unsigned char *c;
    size_t n = strlen(url);

    if ((n == 0U) || (n > GS_PATH_MAX) || (url[0] == '-')) {
        return 0;
    }
    for (c = (const unsigned char *)url; *c != 0U; c++) {
        if ((*c < 0x20U) || (*c == 0x7FU)) {
            return 0;
        }
    }
    return 1;
}

eGsErr_t remote_url(const char *name, Str_t *url)
{
    const char *args[] = { "remote", "get-url", "--", name, NULL };
    eGsErr_t err;

    str_reset(url);
    err = git_run(args, PROC_STDERR_NULL, url);
    if ((err == GS_OK) && !url_ok(str_cstr(url))) {
        err = GS_ERR_FORMAT;
    }
    if (err != GS_OK) {
        str_reset(url);
    }
    return err;
}

/*
 * URL dei blob "creator" gia' letti. Un blob non cambia mai (l'oid e' il
 * suo contenuto), quindi la copia vale per tutto il processo: piu' file con
 * lo stesso creatore costano un solo cat-file blob. Memoria statica e
 * limitata (D-066): oltre REMOTE_URL_CACHE creatori diversi si rilegge.
 * Solo URL validi (url_ok: al piu' GS_PATH_MAX caratteri).
 */
#define REMOTE_URL_CACHE 4U
static Oid_t url_cache_blob[REMOTE_URL_CACHE];
static char url_cache_text[REMOTE_URL_CACHE][GS_PATH_MAX + 1U];
static size_t url_cache_n = 0U;

eGsErr_t remote_creator_url(GitBatch_t *b, const Oid_t *version, Str_t *url)
{
    const char *args[] = { "cat-file", "blob", NULL, NULL };
    char hex[OID_HEX_SIZE];
    Oid_t blob;
    size_t k;
    eGsErr_t err = GS_OK;

    str_reset(url);
    chain_creator(b, version, NULL, &blob);
    if (blob.len == 0U) {
        return GS_OK; /* nessun creatore */
    }
    for (k = 0U; k < url_cache_n; k++) {
        if (oid_eq(&url_cache_blob[k], &blob)) {
            str_add(url, url_cache_text[k]);
            return GS_OK;
        }
    }
    oid_to_hex(&blob, hex);
    args[2] = hex;
    err = git_run(args, 0U, url); /* senza il fine riga finale */
    /* Un byte nullo dentro il blob accorcerebbe l'URL: non valido. */
    if ((err == GS_OK) &&
        ((strlen(str_cstr(url)) != url->len) || !url_ok(str_cstr(url)))) {
        err = GS_ERR_FORMAT;
    }
    if (err != GS_OK) {
        str_reset(url);
    } else if (url_cache_n < REMOTE_URL_CACHE) {
        url_cache_blob[url_cache_n] = blob;
        memcpy(url_cache_text[url_cache_n], str_cstr(url), url->len + 1U);
        url_cache_n++;
    } else {
        /* cache piena: la prossima volta si rilegge */
    }
    return err;
}

eGsErr_t remote_url_blob(const char *url, Oid_t *blob)
{
    static const char *const args[] = { "hash-object", "-w", "--stdin", NULL };
    Str_t out = STR_INIT;
    Proc_t p;
    int code = 0;
    eGsErr_t err;

    if (!url_ok(url)) {
        return GS_ERR_FORMAT;
    }
    err = git_start(&p, args, PROC_STDIN_PIPE);
    if (err != GS_OK) {
        goto exit;
    }
    if (fprintf(p.in, "%s\n", url) < 0) {
        err = GS_ERR_GIT;
    }
    proc_close_in(&p);
    if (proc_collect(&p, &out, &code) != GS_OK) {
        err = GS_ERR_PROC;
    }
    str_trim_eol(&out);
    if ((err == GS_OK) && ((code != 0) || !oid_from_hex(blob, out.buf, out.len))) {
        err = GS_ERR_GIT;
    }

exit:
    str_free(&out);
    return err;
}

/*
 * Un fetch delle catene dell'elenco nelle ref dell'ultima versione vista,
 * con i refspec sullo stdin (fetch --stdin, git >= 2.29): nessun limite
 * della riga di comando. sel = NULL: tutti gli ID; altrimenti quelli con
 * sel[k] != 0. 1 se riesce.
 */
static int fetch_specs(const char *url, const Str_t *ids, const uint8_t *sel)
{
    const char *args[] = { "fetch", "-q", "--no-tags", "--no-write-fetch-head",
                           "--no-auto-gc", "--stdin", url, NULL };
    Str_t out = STR_INIT;
    Proc_t p;
    const char *id;
    size_t k;
    int code = -1;
    int ok = 0;

    if (git_start(&p, args, PROC_STDIN_PIPE | PROC_STDERR_NULL) != GS_OK) {
        goto exit;
    }
    for (k = 0U, id = str_znext(ids, NULL); id != NULL;
         k++, id = str_znext(ids, id)) {
        if ((sel == NULL) || (sel[k] != 0U)) {
            (void)fprintf(p.in, "+refs/shared/%s:refs/shared-creator/%s\n", id, id);
        }
    }
    proc_close_in(&p);
    ok = (proc_collect(&p, &out, &code) == GS_OK) && (code == 0);

exit:
    str_free(&out);
    return ok;
}

/*
 * Quali ID dell'elenco ha la repo remota: has[k] (ls-remote delle sole
 * refs/shared/). 0 se la repo non si raggiunge.
 */
static int remote_has(const char *url, const Str_t *ids, uint8_t *has)
{
    static const char prefix[] = "refs/shared/";
    const char *args[] = { "ls-remote", url, "refs/shared/*", NULL };
    Str_t out = STR_INIT;
    const char *line;
    const char *tab;
    const char *nl;
    const char *id;
    size_t len;
    size_t k;
    int ok;

    for (k = 0U, id = str_znext(ids, NULL); id != NULL;
         k++, id = str_znext(ids, id)) {
        has[k] = 0U;
    }
    ok = (git_run(args, PROC_STDERR_NULL, &out) == GS_OK);
    /* Righe "<oid>\t<ref>": il pattern di ls-remote confronta la parte
     * finale del nome, quindi si cerca la ref esatta. */
    for (line = str_cstr(&out); ok && (*line != '\0');
         line = (*nl == '\n') ? (nl + 1) : nl) {
        nl = strchr(line, '\n');
        nl = (nl != NULL) ? nl : (line + strlen(line));
        tab = memchr(line, '\t', (size_t)(nl - line));
        if ((tab == NULL) ||
            (strncmp(tab + 1, prefix, sizeof prefix - 1U) != 0)) {
            continue;
        }
        tab += sizeof prefix; /* il nome dopo refs/shared/ */
        len = (size_t)(nl - tab);
        for (k = 0U, id = str_znext(ids, NULL); id != NULL;
             k++, id = str_znext(ids, id)) {
            if ((strlen(id) == len) && (strncmp(id, tab, len) == 0)) {
                has[k] = 1U;
            }
        }
    }
    str_free(&out);
    return ok;
}

void remote_fetch_seen(const char *url, const Str_t *ids, uint8_t *res)
{
    size_t n = str_zcount(ids);
    uint8_t *has = xmalloc((n > 0U) ? n : 1U);
    size_t k;
    int any = 0;

    for (k = 0U; k < n; k++) {
        res[k] = (uint8_t)REMOTE_UNREACHABLE;
    }
    if ((n == 0U) || !url_ok(url)) {
        goto exit;
    }
    if (fetch_specs(url, ids, NULL)) {
        for (k = 0U; k < n; k++) {
            res[k] = (uint8_t)REMOTE_GOT;
        }
        goto exit;
    }
    /* Il fetch fallisce tutto se manca anche una sola ref: si chiede quali
     * ci sono e si riprova con quelle. */
    if (!remote_has(url, ids, has)) {
        goto exit;
    }
    for (k = 0U; k < n; k++) {
        if (has[k] == 0U) {
            res[k] = (uint8_t)REMOTE_MISSING;
        } else {
            any = 1;
        }
    }
    if ((any != 0) && fetch_specs(url, ids, has)) {
        for (k = 0U; k < n; k++) {
            if (has[k] != 0U) {
                res[k] = (uint8_t)REMOTE_GOT;
            }
        }
    }

exit:
    free(has);
}

/* Un push di n versioni; idv[k] = ID della versione v[k]. */
static void push_chunk(const char *url, const char *const *idv, const Oid_t *v,
                       size_t n, uint8_t *res)
{
    /* --no-verify: non e' un push dell'utente, il suo pre-push non gira
     * (D-080). Senza "+": un push che non e' fast-forward lo rifiuta git. */
    const char **args = xmalloc((n + 5U) * sizeof *args);
    Str_t specs = STR_INIT;
    Str_t one = STR_INIT;
    Str_t out = STR_INIT;
    char hex[OID_HEX_SIZE];
    const char *s;
    char *line;
    char *nl;
    size_t na = 0U;
    size_t k;
    int code = -1;

    for (k = 0U; k < n; k++) {
        res[k] = (uint8_t)REMOTE_PUSH_REFUSED;
    }
    if (!url_ok(url)) {
        goto exit;
    }
    args[na++] = "push";
    args[na++] = "--porcelain";
    args[na++] = "--no-verify";
    args[na++] = url;
    for (k = 0U; k < n; k++) {
        oid_to_hex(&v[k], hex);
        str_reset(&one);
        str_addf(&one, "%s:refs/shared/%s", hex, idv[k]);
        str_addz(&specs, str_cstr(&one));
    }
    for (s = str_znext(&specs, NULL); s != NULL; s = str_znext(&specs, s)) {
        args[na++] = s;
    }
    args[na] = NULL;
    if (git_capture(args, PROC_STDERR_NULL, &out, &code) != GS_OK) {
        goto exit;
    }
    /* --porcelain: una riga per ref, "<flag>\t<da>:<a>\t<esito>"; "!" se
     * rifiutata. L'esito dice chi l'ha rifiutata (A-28): "[rejected]" il
     * client, perche' la repo ha una versione che qui manca; "[remote
     * rejected]" il server. Nessuna riga: repo irraggiungibile. */
    for (line = out.buf; (line != NULL) && (*line != '\0');
         line = (nl != NULL) ? (nl + 1) : NULL) {
        nl = strchr(line, '\n');
        if (nl != NULL) {
            *nl = '\0';
        }
        for (k = 0U; k < n; k++) {
            str_reset(&one);
            str_addf(&one, ":refs/shared/%s\t", idv[k]);
            if (strstr(line, str_cstr(&one)) == NULL) {
                continue;
            }
            if (line[0] != '!') {
                res[k] = (uint8_t)REMOTE_PUSH_OK;
            } else if (strstr(line, "\t[rejected]") != NULL) {
                res[k] = (uint8_t)REMOTE_PUSH_NEWER;
            } else {
                /* [remote rejected], permessi: la versione resta qui */
            }
        }
    }

exit:
    free((void *)args);
    str_free(&out);
    str_free(&one);
    str_free(&specs);
}

void remote_push_many(const char *url, const Str_t *ids, const Oid_t *v,
                      uint8_t *res)
{
    const char *idv[REMOTE_PUSH_CHUNK];
    const char *id;
    size_t start = 0U;
    size_t k = 0U;

    for (id = str_znext(ids, NULL); id != NULL; id = str_znext(ids, id)) {
        idv[k - start] = id;
        k++;
        if ((k - start) == REMOTE_PUSH_CHUNK) {
            push_chunk(url, idv, &v[start], k - start, &res[start]);
            start = k;
        }
    }
    if (k > start) {
        push_chunk(url, idv, &v[start], k - start, &res[start]);
    }
}

eRemotePush_t remote_push(const char *url, const char *id, const Oid_t *v)
{
    uint8_t res = (uint8_t)REMOTE_PUSH_REFUSED;

    push_chunk(url, &id, v, 1U, &res);
    return (eRemotePush_t)res;
}

void remote_seen_ref(Str_t *ref, const char *id)
{
    str_reset(ref);
    str_addf(ref, "refs/shared-creator/%s", id);
}

eGsErr_t remote_fetch(const char *url, const char *id, const char *ref)
{
    const char *args[] = { "fetch", "-q", "--no-tags", "--no-write-fetch-head",
                           "--no-auto-gc", url, NULL, NULL };
    Str_t spec = STR_INIT;
    eGsErr_t err = GS_ERR_GIT;

    if (url_ok(url)) {
        str_addf(&spec, "+refs/shared/%s:%s", id, ref);
        args[6] = str_cstr(&spec);
        err = git_run(args, PROC_STDERR_NULL, NULL);
    }
    str_free(&spec);
    return err;
}
