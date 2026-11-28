/*
 * auth.c - creatore e sovrani di una versione: l'autorita' (D-111).
 */
#include "auth.h"

#include <stdio.h>
#include <string.h>

#include "bounds.h"
#include "merge.h"
#include "msg.h"
#include "remote.h"

/* Dimensione massima dei blob delle voci: un URL con il fine riga, e
 * GS_SOVEREIGN_MAX URL. Controllata prima di leggere (cat-file -s). */
#define AUTH_CREATOR_MAX    ((unsigned long)GS_PATH_MAX + 1UL)
#define AUTH_SOVEREIGNS_MAX ((unsigned long)GS_SOVEREIGN_MAX * AUTH_CREATOR_MAX)

/*
 * Blob gia' letti in questo processo: piu' file, e molte versioni dello
 * stesso file, hanno le stesse voci. Pochi elementi, riusati in giro.
 */
#define AUTH_CACHE 8U
static Oid_t cache_blob[AUTH_CACHE];
static Str_t cache_text[AUTH_CACHE];
static unsigned long cache_size[AUTH_CACHE]; /* dimensione del blob */
static size_t cache_n = 0U;
static size_t cache_next = 0U;

void auth_free(Auth_t *a)
{
    str_free(&a->creator);
    str_free(&a->sovereigns);
    a->n = 0U;
}

static void auth_reset(Auth_t *a)
{
    str_reset(&a->creator);
    str_reset(&a->sovereigns);
    a->n = 0U;
}

void auth_blobs(GitBatch_t *b, const Oid_t *v, AuthBlobs_t *out)
{
    char hex[OID_HEX_SIZE];
    Str_t name = STR_INIT;

    out->creator.len = 0U;
    out->sovereigns.len = 0U;
    out->heirs.len = 0U;
    if ((v == NULL) || (v->len == 0U)) {
        return;
    }
    oid_to_hex(v, hex);
    str_addf(&name, "%s:creator", hex);
    if (git_batch_blob(b, str_cstr(&name), &out->creator) == 0) {
        out->creator.len = 0U;
    }
    str_reset(&name);
    str_addf(&name, "%s:sovereigns", hex);
    if (git_batch_blob(b, str_cstr(&name), &out->sovereigns) == 0) {
        out->sovereigns.len = 0U;
    }
    str_reset(&name);
    str_addf(&name, "%s:heirs", hex);
    if (git_batch_blob(b, str_cstr(&name), &out->heirs) == 0) {
        out->heirs.len = 0U;
    }
    str_free(&name);
}

/* Due voci uguali: lo stesso blob, o assenti entrambe (oid_eq non
 * considera uguali due oid vuoti). */
static int entry_eq(const Oid_t *a, const Oid_t *b)
{
    return ((a->len == 0U) && (b->len == 0U)) || oid_eq(a, b);
}

int auth_blobs_eq(const AuthBlobs_t *a, const AuthBlobs_t *b)
{
    return entry_eq(&a->creator, &b->creator) && entry_eq(&a->sovereigns, &b->sovereigns);
}

int auth_blobs_eq_all(const AuthBlobs_t *a, const AuthBlobs_t *b)
{
    return auth_blobs_eq(a, b) && entry_eq(&a->heirs, &b->heirs);
}

eGsErr_t auth_blob_text(GitBatch_t *b, const Oid_t *blob, unsigned long max,
                        Str_t *out)
{
    const char *args[] = { "cat-file", "blob", NULL, NULL };
    char hex[OID_HEX_SIZE];
    Oid_t got;
    unsigned long size;
    size_t k;
    eGsErr_t err;

    str_reset(out);
    for (k = 0U; k < cache_n; k++) {
        if (oid_eq(&cache_blob[k], blob)) {
            /* B-SA2: il limite vale anche per un blob gia' letto con un
             * limite piu' largo (heirs, poi creator) */
            if (cache_size[k] > max) {
                return GS_ERR_FORMAT;
            }
            str_addn(out, str_cstr(&cache_text[k]), cache_text[k].len);
            return GS_OK;
        }
    }
    oid_to_hex(blob, hex);
    /* prima la dimensione: un blob enorme non si legge */
    if ((git_batch_query(b, hex, &got) != GIT_OBJ_BLOB) || (b->size > max)) {
        return GS_ERR_FORMAT;
    }
    size = (unsigned long)b->size;
    args[2] = hex;
    err = git_run(args, 0U, out);
    /* un byte nullo accorcerebbe il testo */
    if ((err == GS_OK) && (strlen(str_cstr(out)) != out->len)) {
        err = GS_ERR_FORMAT;
    }
    /* B-SA6: git_run toglie tutti i fine riga finali; la forma scritta da
     * git shared ne ha uno solo, "\n". Un blob "url\n\n\r\n" e' un'altra
     * forma della stessa voce: non vale. */
    if ((err == GS_OK) && (((unsigned long)out->len + 1UL) != size)) {
        err = GS_ERR_FORMAT;
    }
    if (err != GS_OK) {
        str_reset(out);
        return err;
    }
    k = (cache_n < AUTH_CACHE) ? cache_n++ : cache_next;
    cache_next = (k + 1U) % AUTH_CACHE;
    cache_blob[k] = *blob;
    cache_size[k] = size;
    str_reset(&cache_text[k]);
    str_addn(&cache_text[k], str_cstr(out), out->len);
    return GS_OK;
}

eGsErr_t auth_read(GitBatch_t *b, const AuthBlobs_t *ab, Auth_t *a)
{
    Str_t text = STR_INIT;
    Str_t one = STR_INIT;
    const char *line;
    const char *nl;
    size_t len;
    eGsErr_t err = GS_OK;

    auth_reset(a);
    if (ab->creator.len > 0U) {
        err = auth_blob_text(b, &ab->creator, AUTH_CREATOR_MAX, &a->creator);
        if ((err == GS_OK) && !remote_url_ok(str_cstr(&a->creator))) {
            err = GS_ERR_FORMAT;
        }
    }
    if ((err == GS_OK) && (ab->sovereigns.len > 0U)) {
        err = auth_blob_text(b, &ab->sovereigns, AUTH_SOVEREIGNS_MAX, &text);
    }
    /* Un URL per riga: nessuna riga vuota, al piu' GS_SOVEREIGN_MAX. */
    for (line = str_cstr(&text); (err == GS_OK) && (text.len > 0U) && (line != NULL);
         line = (nl != NULL) ? (nl + 1) : NULL) {
        nl = strchr(line, '\n');
        len = (nl != NULL) ? (size_t)(nl - line) : strlen(line);
        str_reset(&one);
        str_addn(&one, line, len);
        if ((len == 0U) || (a->n >= GS_SOVEREIGN_MAX) || !remote_url_ok(str_cstr(&one))) {
            err = GS_ERR_FORMAT;
        } else {
            str_addz(&a->sovereigns, str_cstr(&one));
            a->n++;
        }
    }
    /* Sovrani senza creatore: non puo' nascere da git shared. */
    if ((err == GS_OK) && (a->n > 0U) && (a->creator.len == 0U)) {
        err = GS_ERR_FORMAT;
    }
    if (err != GS_OK) {
        auth_reset(a);
    }
    str_free(&one);
    str_free(&text);
    return err;
}

eGsErr_t auth_of(GitBatch_t *b, const Oid_t *v, Auth_t *a, AuthBlobs_t *ab)
{
    AuthBlobs_t mine;
    AuthBlobs_t *p = (ab != NULL) ? ab : &mine;

    auth_blobs(b, v, p);
    return auth_read(b, p, a);
}

/* L'ultimo elemento di un elenco compatto di n elementi. */
static const char *last_of(const Str_t *list)
{
    const char *p;
    const char *last = NULL;

    for (p = str_znext(list, NULL); p != NULL; p = str_znext(list, p)) {
        last = p;
    }
    return last;
}

const char *auth_authority(const Auth_t *a)
{
    const char *last = (a->n > 0U) ? last_of(&a->sovereigns) : NULL;

    return (last != NULL) ? last : str_cstr(&a->creator);
}

/* Due URL, anche vuoti: "" e' uguale solo a "". Confronto stretto (S-06):
 * decide se un cambio di autorita' si accetta. */
static int url_same(const char *x, const char *y)
{
    if ((*x == '\0') || (*y == '\0')) {
        return (*x == '\0') && (*y == '\0');
    }
    return remote_same_strict(x, y);
}

eGsErr_t auth_target(GitBatch_t *b, const Oid_t *v, Str_t *url, int *sovereign)
{
    Auth_t a = AUTH_INIT;
    eGsErr_t err = auth_of(b, v, &a, NULL);

    str_reset(url);
    *sovereign = 0;
    if ((err == GS_OK) && (a.creator.len > 0U)) {
        str_add(url, auth_authority(&a));
        *sovereign = (a.n > 0U) && !remote_same(str_cstr(url), str_cstr(&a.creator));
    }
    auth_free(&a);
    return err;
}

const char *auth_role(int sovereign)
{
    return msg((sovereign != 0) ? MSG_ROLE_SOVEREIGN : MSG_ROLE_CREATOR);
}

int auth_creator_same(const Auth_t *a, const Auth_t *b)
{
    return url_same(str_cstr(&a->creator), str_cstr(&b->creator));
}

int auth_same(const Auth_t *a, const Auth_t *b)
{
    return url_same(str_cstr(&a->creator), str_cstr(&b->creator)) &&
           url_same(auth_authority(a), auth_authority(b));
}

eGsErr_t auth_crown(const Auth_t *cur, const AuthBlobs_t *curb, const char *url,
                    AuthBlobs_t *out)
{
    static const char *const args[] = { "hash-object", "-w", "--stdin", NULL };
    const char *keep[GS_SOVEREIGN_MAX];
    const char *p;
    Str_t res = STR_INIT;
    Proc_t proc;
    size_t n = 0U;
    size_t k;
    size_t from;
    int code = 0;
    eGsErr_t err;

    if (!remote_url_ok(url)) {
        return GS_ERR_FORMAT;
    }
    /* L'elenco senza url (anche scritto in un'altra forma), poi url in
     * fondo; oltre il limite esce il piu' vecchio. */
    for (p = str_znext(&cur->sovereigns, NULL); (p != NULL) && (n < GS_SOVEREIGN_MAX);
         p = str_znext(&cur->sovereigns, p)) {
        if (!remote_same(p, url)) {
            keep[n++] = p;
        }
    }
    from = (n >= GS_SOVEREIGN_MAX) ? ((n + 1U) - GS_SOVEREIGN_MAX) : 0U;
    out->creator = curb->creator;
    out->heirs = curb->heirs;
    err = git_start(&proc, args, PROC_STDIN_PIPE);
    if (err != GS_OK) {
        return err;
    }
    for (k = from; k < n; k++) {
        if (fprintf(proc.in, "%s\n", keep[k]) < 0) {
            err = GS_ERR_GIT;
        }
    }
    if (fprintf(proc.in, "%s\n", url) < 0) {
        err = GS_ERR_GIT;
    }
    proc_close_in(&proc);
    if (proc_collect(&proc, &res, &code) != GS_OK) {
        err = GS_ERR_PROC;
    }
    str_trim_eol(&res);
    if ((err == GS_OK) &&
        ((code != 0) || !oid_from_hex(&out->sovereigns, res.buf, res.len))) {
        err = GS_ERR_GIT;
    }
    str_free(&res);
    return err;
}

eGsErr_t auth_merge(GitBatch_t *b, const Oid_t *ours, const Oid_t *theirs,
                    AuthBlobs_t *out, int *conflict)
{
    AuthBlobs_t o;
    AuthBlobs_t t;
    AuthBlobs_t base;
    Auth_t ao = AUTH_INIT;
    Auth_t at = AUTH_INIT;
    Auth_t abase = AUTH_INIT;
    Oid_t mb;
    eGsErr_t err = GS_OK;

    *conflict = 0;
    auth_blobs(b, ours, &o);
    *out = o;
    if ((theirs == NULL) || (theirs->len == 0U)) {
        return GS_OK;
    }
    auth_blobs(b, theirs, &t);
    if (auth_blobs_eq_all(&o, &t)) {
        return GS_OK; /* il caso comune: nessun processo */
    }
    err = merge_base(ours, theirs, &mb);
    if (err != GS_OK) {
        goto exit;
    }
    auth_blobs(b, (mb.len > 0U) ? &mb : NULL, &base);
    /* Gli eredi (D-114): cambiati da un solo lato -> quel lato; da
     * entrambi -> il lato ricevuto. */
    if (!entry_eq(&o.heirs, &t.heirs) && !entry_eq(&base.heirs, &t.heirs)) {
        out->heirs = t.heirs;
    }
    if (auth_blobs_eq(&o, &t) || auth_blobs_eq(&base, &t)) {
        goto exit; /* uguali, o cambiata solo da ours */
    }
    if (auth_blobs_eq(&base, &o)) {
        out->creator = t.creator; /* cambiata solo da theirs */
        out->sovereigns = t.sovereigns;
        goto exit;
    }
    /* Blob diversi: lo stesso URL puo' essere scritto in due forme. Una
     * voce non valida vale come cambiata. */
    (void)auth_read(b, &o, &ao);
    (void)auth_read(b, &t, &at);
    (void)auth_read(b, &base, &abase);
    if (auth_same(&ao, &at) || auth_same(&abase, &at)) {
        /* uguali, o theirs come la base: resta ours */
    } else if (auth_same(&abase, &ao)) {
        out->creator = t.creator;
        out->sovereigns = t.sovereigns;
    } else {
        *conflict = 1;
    }

exit:
    auth_free(&ao);
    auth_free(&at);
    auth_free(&abase);
    return err;
}

void auth_key(const Auth_t *a, int with_creator, Str_t *key)
{
    str_reset(key);
    str_add(key, auth_authority(a));
    if (with_creator != 0) {
        str_addf(key, "\t%s", str_cstr(&a->creator));
    }
}

void auth_key_authority(const char *key, Str_t *url)
{
    const char *tab = strchr(key, '\t');

    str_reset(url);
    str_addn(url, key, (tab != NULL) ? (size_t)(tab - key) : strlen(key));
}

int auth_key_matches(const Auth_t *a, const char *key)
{
    const char *tab = strchr(key, '\t');
    Str_t url = STR_INIT;
    int ok;

    auth_key_authority(key, &url);
    ok = (url.len > 0U) && url_same(str_cstr(&url), auth_authority(a)) &&
         ((tab == NULL) || url_same(tab + 1, str_cstr(&a->creator)));
    str_free(&url);
    return ok;
}
