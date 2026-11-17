/*
 * hooks.c - installazione e rimozione dei blocchi negli hook.
 */
#include "hooks.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "git.h"
#include "msg.h"
#include "path.h"
#include "sys.h"

#define HOOK_BEGIN "# >>> git shared >>>"
#define HOOK_END   "# <<< git shared <<<"
#define HOOK_SHEBANG "#!/bin/sh\n"

/* want = 0: il blocco si toglie se c'e' (hook di versioni precedenti). */
static const struct {
    const char *event;
    unsigned want;
} hooks[] = {
    { "pre-commit", 0U },  /* installato dalla prima versione di M3 (D-073) */
    { "post-commit", 0U }, /* installato con shared.autoPublish (D-074) */
    { "post-checkout", 1U },
    { "post-merge", 1U },
    { "post-rewrite", 1U },
};

/*
 * Cartella degli hook: `rev-parse --git-path hooks` dalla radice, che
 * rispetta core.hooksPath. Il risultato puo' essere relativo alla radice.
 */
static eGsErr_t hooks_dir(const Repo_t *r, Str_t *dir)
{
    const char *args[] = { "-C", NULL, "rev-parse", "--git-path", "hooks", NULL };
    Str_t out = STR_INIT;
    eGsErr_t err;

    args[1] = str_cstr(&r->top);
    err = git_run(args, 0U, &out);
    if (err == GS_OK) {
        str_reset(dir);
        if (!path_is_absolute(str_cstr(&out))) {
            str_addf(dir, "%s/", str_cstr(&r->top));
        }
        str_add(dir, str_cstr(&out));
    }
    str_free(&out);
    return err;
}

/* Contenuto del file; un file assente da' contenuto vuoto e 0. */
static int read_file(const char *path, Str_t *out)
{
    char chunk[1024];
    size_t n;
    FILE *f = sys_fopen(path, "rb");
    int found = (f != NULL);

    str_reset(out);
    if (found != 0) {
        while ((n = fread(chunk, 1U, sizeof chunk, f)) > 0U) {
            str_addn(out, chunk, n);
        }
        (void)fclose(f);
    }
    return found;
}

/*
 * Riscrive il file sul posto: un hook che esisteva tiene il suo modo (un
 * hook non eseguibile e' un hook che l'utente ha disattivato). Il bit di
 * esecuzione si mette solo a un file creato adesso (created != 0).
 */
static eGsErr_t write_file(const char *path, const Str_t *text, int created)
{
    FILE *f = sys_fopen(path, "wb");
    int put_rc;
    int close_rc;
    eGsErr_t err = GS_ERR_SYS;

    if (f != NULL) {
        put_rc = fputs(str_cstr(text), f);
        close_rc = fclose(f);
        if ((put_rc >= 0) && (close_rc == 0) &&
            ((created == 0) || (sys_make_executable(path) == 0))) {
            err = GS_OK;
        }
    }
    return err;
}

static int is_blank(char c)
{
    return (c == ' ') || (c == '\t') || (c == '\r');
}

/*
 * 1 se l'hook e' uno script di shell, in cui il blocco (codice sh) si puo'
 * inserire (reportIdea C-02, D-096). Vuoto o assente: si crea uno script
 * sh. Con un byte nullo e' un binario: no. Senza "#!" git lo esegue con
 * sh: si'. Con "#!" conta l'interprete, cioe' il nome finale del primo
 * argomento; dopo "env" o "busybox" quello dell'argomento successivo, senza
 * le opzioni ("env -S bash"). ".exe" in fondo non conta.
 */
static int hook_is_shell(const Str_t *old)
{
    static const char *const shells[] = { "sh", "bash", "dash", "zsh",
                                          "ksh", "mksh", "ash" };
    char word[32];
    const char *p;
    const char *end;
    const char *tok;
    size_t len;
    size_t k;
    int next = 1; /* si guarda ancora un argomento */
    int shell = 0;

    if (old->len == 0U) {
        return 1;
    }
    if (memchr(old->buf, '\0', old->len) != NULL) {
        return 0;
    }
    if (strncmp(old->buf, "#!", 2U) != 0) {
        return 1;
    }
    p = old->buf + 2;
    end = memchr(p, '\n', old->len - 2U);
    end = (end != NULL) ? end : (old->buf + old->len);
    while ((next != 0) && (p < end)) {
        while ((p < end) && is_blank(*p)) {
            p++;
        }
        tok = p;
        while ((p < end) && !is_blank(*p)) {
            p++;
        }
        len = (size_t)(p - tok);
        if ((len == 0U) || (tok[0] == '-')) {
            next = (len != 0U); /* fine riga: nessun interprete */
            continue;           /* opzione di env: si salta */
        }
        for (k = len; (k > 0U) && (tok[k - 1U] != '/') && (tok[k - 1U] != '\\'); k--) {
        }
        tok += k;
        len -= k;
        if ((len > 4U) && (tok[len - 4U] == '.') &&
            (tolower((unsigned char)tok[len - 3U]) == 'e') &&
            (tolower((unsigned char)tok[len - 2U]) == 'x') &&
            (tolower((unsigned char)tok[len - 1U]) == 'e')) {
            len -= 4U;
        }
        next = 0;
        if (len < sizeof word) {
            memcpy(word, tok, len);
            word[len] = '\0';
            if ((strcmp(word, "env") == 0) || (strcmp(word, "busybox") == 0)) {
                next = 1;
            }
            for (k = 0U; k < (sizeof shells / sizeof shells[0]); k++) {
                shell = shell || (strcmp(word, shells[k]) == 0);
            }
        }
    }
    return shell;
}

static eGsErr_t install_one(const char *path, const Str_t *old, int exists,
                            const char *event)
{
    Str_t text = STR_INIT;
    const char *body = str_cstr(old);
    const char *nl;
    eGsErr_t err;

    if (old->len == 0U) {
        str_add(&text, HOOK_SHEBANG);
    } else if (strncmp(body, "#!", 2U) == 0) {
        nl = strchr(body, '\n');
        str_addn(&text, body, (nl != NULL) ? (size_t)(nl - body) + 1U : old->len);
        if (nl == NULL) {
            str_addch(&text, '\n');
        }
        body = (nl != NULL) ? (nl + 1) : "";
    } else {
        /* senza riga "#!": il blocco va in testa */
    }
    /* "git-shared" diretto e non "git shared": un processo in meno per
     * hook; git mette nel PATH degli hook le cartelle in cui lo trova.
     * "</dev/null": lo stdin resta all'hook che ci ospita (es. la lista
     * di post-rewrite). "|| :": senza git shared installato, o se
     * fallisse, l'esito dell'hook non cambia. */
    str_addf(&text, HOOK_BEGIN "\ngit-shared hook %s \"$@\" </dev/null || :\n"
                    HOOK_END "\n", event);
    str_add(&text, body);
    err = write_file(path, &text, (exists == 0));
    str_free(&text);
    return err;
}

/* Toglie il blocco (marker compresi). Se resta solo "#!/bin/sh", l'hook
 * l'aveva creato git shared: il file si cancella. */
static eGsErr_t remove_one(const char *path, const Str_t *old)
{
    Str_t text = STR_INIT;
    const char *body = str_cstr(old);
    const char *begin = strstr(body, HOOK_BEGIN);
    const char *end = strstr(body, HOOK_END);
    const char *after;
    eGsErr_t err = GS_OK;

    if ((begin != NULL) && (end != NULL) && (end > begin)) {
        after = strchr(end, '\n');
        after = (after != NULL) ? (after + 1) : (end + strlen(end));
        str_addn(&text, body, (size_t)(begin - body));
        str_add(&text, after);
        if ((text.len == 0U) || str_eq(&text, HOOK_SHEBANG)) {
            err = (sys_remove(path) == 0) ? GS_OK : GS_ERR_SYS;
        } else {
            err = write_file(path, &text, 0);
        }
    }
    str_free(&text);
    return err;
}

eGsErr_t hooks_sync(const Repo_t *r)
{
    Str_t dir = STR_INIT;
    Str_t path = STR_INIT;
    Str_t old = STR_INIT;
    size_t i;
    int want;
    int has;
    int exists;
    int shell;
    eGsErr_t one;
    eGsErr_t err = hooks_dir(r, &dir);

    if (err != GS_OK) {
        msg_warning(MSG_WARN_HOOK, "hooks");
    }
    for (i = 0U; (err == GS_OK) && (i < (sizeof hooks / sizeof hooks[0])); i++) {
        str_reset(&path);
        str_addf(&path, "%s/%s", str_cstr(&dir), hooks[i].event);
        exists = read_file(str_cstr(&path), &old);
        want = (hooks[i].want != 0U);
        has = (strstr(str_cstr(&old), HOOK_BEGIN) != NULL);
        shell = hook_is_shell(&old);
        one = GS_OK;
        if (want && !has && shell) {
            one = install_one(str_cstr(&path), &old, exists, hooks[i].event);
        } else if (want && !has) {
            /* Hook Python, Perl, binario...: non si tocca (C-02). Gli
             * avvisi sono una comodita' (D-041): mancano solo loro. */
            msg_warning(MSG_WARN_HOOK_NOT_SHELL, str_cstr(&path));
        } else if (has && (!want || !shell)) {
            /* Blocco non piu' usato, oppure messo in un hook non di shell
             * da una versione precedente, che cosi' torna com'era. */
            one = remove_one(str_cstr(&path), &old);
            if ((one == GS_OK) && !shell) {
                msg_warning(MSG_WARN_HOOK_REPAIRED, str_cstr(&path));
            }
        } else {
            /* gia' come deve essere */
        }
        if (one != GS_OK) {
            msg_warning(MSG_WARN_HOOK, str_cstr(&path));
            err = GS_ERR_SYS;
        }
    }
    str_free(&old);
    str_free(&path);
    str_free(&dir);
    return err;
}
