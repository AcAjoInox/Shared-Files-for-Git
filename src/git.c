/*
 * git.c - accesso a git tramite i suoi comandi.
 */
#include "git.h"

#include <stdlib.h>
#include <string.h>

#include "mem.h"
#include "sys.h"

#if defined(_WIN32)
#define GIT_EXE "git.exe"
#else
#define GIT_EXE "git"
#endif

/* ---- Oid ---------------------------------------------------------------- */

static int hex_digit(char c)
{
    int v = -1;

    if ((c >= '0') && (c <= '9')) {
        v = c - '0';
    } else if ((c >= 'a') && (c <= 'f')) {
        v = (c - 'a') + 10;
    } else if ((c >= 'A') && (c <= 'F')) {
        v = (c - 'A') + 10;
    } else {
        /* non esadecimale */
    }
    return v;
}

int oid_from_hex(Oid_t *oid, const char *hex, size_t len)
{
    size_t i;
    int hi;
    int lo;
    int ok = (len == (2U * GS_OID_RAW_SHA1)) || (len == GS_OID_HEX_MAX);

    for (i = 0U; ok && (i < (len / 2U)); i++) {
        hi = hex_digit(hex[2U * i]);
        lo = hex_digit(hex[(2U * i) + 1U]);
        ok = (hi >= 0) && (lo >= 0);
        if (ok) {
            oid->raw[i] = (uint8_t)((unsigned)hi << 4U | (unsigned)lo);
        }
    }
    oid->len = ok ? (uint8_t)(len / 2U) : 0U;
    return ok;
}

void oid_to_hex(const Oid_t *oid, char hex[OID_HEX_SIZE])
{
    static const char digits[] = "0123456789abcdef";
    size_t i;

    for (i = 0U; i < oid->len; i++) {
        hex[2U * i] = digits[oid->raw[i] >> 4U];
        hex[(2U * i) + 1U] = digits[oid->raw[i] & 0x0FU];
    }
    hex[2U * (size_t)oid->len] = '\0';
}

int oid_eq(const Oid_t *a, const Oid_t *b)
{
    return (a->len != 0U) && (a->len == b->len) &&
           (memcmp(a->raw, b->raw, a->len) == 0);
}

void oidlist_add(OidList_t *l, const Oid_t *oid)
{
    if (l->n == l->cap) {
        l->cap = (l->cap == 0U) ? 16U : (l->cap * 2U);
        l->v = xrealloc(l->v, l->cap * sizeof *l->v);
    }
    l->v[l->n] = *oid;
    l->n++;
}

int oidlist_has(const OidList_t *l, const Oid_t *oid)
{
    size_t i;
    int found = 0;

    for (i = 0U; (i < l->n) && (found == 0); i++) {
        found = oid_eq(&l->v[i], oid);
    }
    return found;
}

void oidlist_free(OidList_t *l)
{
    free(l->v);
    l->v = NULL;
    l->n = 0U;
    l->cap = 0U;
}

/* ---- Processi git ------------------------------------------------------ */

/* Programma git da lanciare (vedi git.h). Calcolato una sola volta. */
static const char *git_program(void)
{
    static char *program = NULL;
    char *exec_path;
    Str_t s = STR_INIT;
    FILE *f;

    if (program == NULL) {
        /* In UTF-8 come ogni path del programma: su Windows getenv la
         * darebbe nella code page ANSI, e con git sotto C:\Users\Jose'
         * non partirebbe nessun comando (reportIdea C-05, D-097). */
        exec_path = sys_getenv("GIT_EXEC_PATH");
        if (exec_path != NULL) {
            str_addf(&s, "%s/%s", exec_path, GIT_EXE);
            free(exec_path);
            f = sys_fopen(str_cstr(&s), "rb");
            if (f != NULL) {
                (void)fclose(f);
                program = xstrdup(str_cstr(&s));
            }
        }
        if (program == NULL) {
            program = xstrdup("git");
        }
        str_free(&s);
    }
    return program;
}

/* argv per proc_*: git_program() seguito da args. Da liberare con free. */
static const char **make_argv(const char *const args[])
{
    size_t n = 0U;
    size_t i;
    const char **argv;

    while (args[n] != NULL) {
        n++;
    }
    argv = xmalloc((n + 2U) * sizeof *argv);
    argv[0] = git_program();
    for (i = 0U; i < n; i++) {
        argv[i + 1U] = args[i];
    }
    argv[n + 1U] = NULL;
    return argv;
}

eGsErr_t git_start(Proc_t *p, const char *const args[], unsigned flags)
{
    const char **argv = make_argv(args);
    eGsErr_t err = proc_start(p, argv, flags);

    free((void *)argv);
    return err;
}

eGsErr_t git_capture(const char *const args[], unsigned flags, Str_t *out,
                     int *exit_code)
{
    const char **argv = make_argv(args);
    eGsErr_t err = proc_capture(argv, flags, out, exit_code);

    free((void *)argv);
    return err;
}

eGsErr_t git_run(const char *const args[], unsigned flags, Str_t *out)
{
    Str_t tmp = STR_INIT;
    Str_t *dst = (out != NULL) ? out : &tmp;
    int code = 0;
    eGsErr_t err = git_capture(args, flags, dst, &code);

    if ((err == GS_OK) && (code != 0)) {
        err = GS_ERR_GIT;
    }
    str_trim_eol(dst);
    str_free(&tmp);
    return err;
}

eGsErr_t git_update_ref(const char *ref, const Oid_t *new_oid,
                        const Oid_t *old_oid, const char *reason)
{
    const char *args[] = { "update-ref", "-m", reason, ref, NULL, NULL, NULL };
    char new_hex[OID_HEX_SIZE];
    char old_hex[OID_HEX_SIZE];

    oid_to_hex(new_oid, new_hex);
    args[4] = new_hex;
    if (old_oid != NULL) {
        oid_to_hex(old_oid, old_hex);
        args[5] = old_hex;
    } else {
        args[5] = ""; /* valore vecchio vuoto: la ref non deve esistere */
    }
    return git_run(args, 0U, NULL);
}

eGsErr_t git_set_ref(const char *ref, const Oid_t *new_oid, const char *reason)
{
    const char *args[] = { "update-ref", "-m", reason, ref, NULL, NULL };
    char new_hex[OID_HEX_SIZE];

    oid_to_hex(new_oid, new_hex);
    args[4] = new_hex;
    return git_run(args, 0U, NULL);
}

eGsErr_t git_delete_refs(const Str_t *refs)
{
    static const char *const args[] = { "update-ref", "--stdin", NULL };
    const char *r;
    Str_t out = STR_INIT;
    Proc_t p;
    int code = 0;
    eGsErr_t err = GS_OK;

    if (refs->len > 0U) {
        err = git_start(&p, args, PROC_STDIN_PIPE);
        if (err == GS_OK) {
            for (r = str_znext(refs, NULL); r != NULL; r = str_znext(refs, r)) {
                (void)fprintf(p.in, "delete %s\n", r);
            }
            proc_close_in(&p);
            err = proc_collect(&p, &out, &code);
            if ((err == GS_OK) && (code != 0)) {
                err = GS_ERR_GIT;
            }
        }
    }
    str_free(&out);
    return err;
}

/* ---- Interrogazioni in serie ------------------------------------------- */

eGsErr_t git_batch_open(GitBatch_t *b)
{
    static const char *const args[] = {
        "cat-file", "--batch-check=%(objecttype) %(objectname)", NULL
    };

    str_init(&b->line);
    return git_start(&b->p, args, PROC_STDIN_PIPE);
}

eGitObj_t git_batch_query(GitBatch_t *b, const char *name, Oid_t *oid)
{
    static const struct {
        const char *name;
        eGitObj_t type;
    } types[] = {
        { "blob", GIT_OBJ_BLOB },
        { "tree", GIT_OBJ_TREE },
        { "commit", GIT_OBJ_COMMIT },
    };
    const char *line;
    const char *sp;
    size_t type_len;
    size_t i;
    eGitObj_t type = GIT_OBJ_ERROR;

    oid->len = 0U;
    /* cat-file svuota il proprio output a ogni risposta: domanda, fflush,
     * risposta, senza rischio di blocco (proc.h). */
    if ((fputs(name, b->p.in) < 0) || (fputc('\n', b->p.in) == EOF) ||
        (fflush(b->p.in) != 0) || (proc_read(&b->p, '\n', &b->line) == 0)) {
        goto exit;
    }
    /* Risposta: "<tipo> <oid>", oppure "<nome> missing" (o "ambiguous").
     * Il nome puo' contenere spazi, l'oid no: conta cio' che segue il
     * primo spazio, che deve essere un oid valido. */
    type = GIT_OBJ_MISSING;
    line = str_cstr(&b->line);
    sp = strchr(line, ' ');
    if ((sp != NULL) && oid_from_hex(oid, sp + 1, strlen(sp + 1))) {
        type = GIT_OBJ_OTHER;
        type_len = (size_t)(sp - line);
        for (i = 0U; i < (sizeof types / sizeof types[0]); i++) {
            if ((strlen(types[i].name) == type_len) &&
                (strncmp(line, types[i].name, type_len) == 0)) {
                type = types[i].type;
            }
        }
    }

exit:
    return type;
}

int git_batch_blob(GitBatch_t *b, const char *name, Oid_t *oid)
{
    return git_batch_query(b, name, oid) == GIT_OBJ_BLOB;
}

void git_batch_close(GitBatch_t *b)
{
    int code = 0;

    (void)proc_finish(&b->p, &code);
    str_free(&b->line);
}
