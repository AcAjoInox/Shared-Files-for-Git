/*
 * remote.c - la catena in remoto: il creatore.
 */
#include "remote.h"

#include <stdio.h>
#include <string.h>

#include "bounds.h"
#include "chain.h"

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

eGsErr_t remote_creator_url(GitBatch_t *b, const Oid_t *version, Str_t *url)
{
    const char *args[] = { "cat-file", "blob", NULL, NULL };
    char hex[OID_HEX_SIZE];
    Oid_t blob;
    eGsErr_t err = GS_OK;

    str_reset(url);
    chain_creator(b, version, NULL, &blob);
    if (blob.len > 0U) {
        oid_to_hex(&blob, hex);
        args[2] = hex;
        err = git_run(args, 0U, url); /* senza il fine riga finale */
        if ((err == GS_OK) && !url_ok(str_cstr(url))) {
            err = GS_ERR_FORMAT;
        }
        if (err != GS_OK) {
            str_reset(url);
        }
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

eGsErr_t remote_tip(const char *url, const char *id, Oid_t *tip)
{
    const char *args[] = { "ls-remote", url, NULL, NULL };
    Str_t ref = STR_INIT;
    Str_t out = STR_INIT;
    const char *line;
    const char *tab;
    const char *nl;
    size_t len;
    eGsErr_t err = GS_ERR_GIT;

    tip->len = 0U;
    if (!url_ok(url)) {
        goto exit;
    }
    str_addf(&ref, "refs/shared/%s", id);
    args[2] = str_cstr(&ref);
    err = git_run(args, PROC_STDERR_NULL, &out);
    if (err != GS_OK) {
        goto exit;
    }
    /* Righe "<oid>\t<ref>": il pattern di ls-remote confronta la parte
     * finale del nome, quindi si cerca la ref esatta. */
    for (line = str_cstr(&out); *line != '\0'; line = (*nl == '\n') ? (nl + 1) : nl) {
        nl = strchr(line, '\n');
        nl = (nl != NULL) ? nl : (line + strlen(line));
        tab = memchr(line, '\t', (size_t)(nl - line));
        if (tab != NULL) {
            len = (size_t)(nl - (tab + 1));
            if ((len == ref.len) && (strncmp(tab + 1, str_cstr(&ref), len) == 0) &&
                !oid_from_hex(tip, line, (size_t)(tab - line))) {
                err = GS_ERR_FORMAT;
            }
        }
    }

exit:
    str_free(&out);
    str_free(&ref);
    return err;
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

eRemotePush_t remote_push(const char *url, const char *id, const Oid_t *v)
{
    /* --no-verify: non e' un push dell'utente, il suo pre-push non gira
     * (D-080). Senza "+": un push che non e' fast-forward lo rifiuta git. */
    const char *args[] = { "push", "--porcelain", "--no-verify", url, NULL,
                           NULL };
    char hex[OID_HEX_SIZE];
    Str_t spec = STR_INIT;
    Str_t out = STR_INIT;
    int code = -1;
    eRemotePush_t res = REMOTE_PUSH_REFUSED;

    if (!url_ok(url)) {
        goto exit;
    }
    oid_to_hex(v, hex);
    str_addf(&spec, "%s:refs/shared/%s", hex, id);
    args[4] = str_cstr(&spec);
    if (git_capture(args, PROC_STDERR_NULL, &out, &code) != GS_OK) {
        goto exit;
    }
    /* --porcelain: una riga per ref, "!" in testa se rifiutata; il motivo
     * dice chi l'ha rifiutata (A-28): "[rejected]" il client, perche' la
     * repo ha una versione che qui manca; "[remote rejected]" il server. */
    if (code == 0) {
        res = REMOTE_PUSH_OK;
    } else if (strstr(str_cstr(&out), "\t[rejected]") != NULL) {
        res = REMOTE_PUSH_NEWER;
    } else {
        /* [remote rejected], permessi, rete: la versione resta qui */
    }

exit:
    str_free(&out);
    str_free(&spec);
    return res;
}

void remote_incoming_ref(Str_t *ref, const char *id)
{
    str_reset(ref);
    str_addf(ref, "refs/shared-incoming/%s/creator", id);
}
