/*
 * git.c - accesso a git tramite i suoi comandi.
 */
#include "git.h"

#include <stdlib.h>

#include "mem.h"

#if defined(_WIN32)
#define GIT_EXE "git.exe"
#else
#define GIT_EXE "git"
#endif

/* Programma git da lanciare (vedi git.h). Calcolato una sola volta. */
static const char *git_program(void)
{
    static char *program = NULL;
    const char *exec_path;
    Str_t s = STR_INIT;
    FILE *f;

    if (program == NULL) {
        exec_path = getenv("GIT_EXEC_PATH");
        if (exec_path != NULL) {
            str_addf(&s, "%s/%s", exec_path, GIT_EXE);
            f = fopen(str_cstr(&s), "rb");
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

gs_err_t git_start(Proc_t *p, const char *const args[], unsigned flags)
{
    const char **argv = make_argv(args);
    gs_err_t err = proc_start(p, argv, flags);

    free((void *)argv);
    return err;
}

gs_err_t git_capture(const char *const args[], unsigned flags, Str_t *out,
                     int *exit_code)
{
    const char **argv = make_argv(args);
    gs_err_t err = proc_capture(argv, flags, out, exit_code);

    free((void *)argv);
    return err;
}

gs_err_t git_toplevel(Str_t *top)
{
    static const char *const args[] = { "rev-parse", "--show-toplevel", NULL };
    int code = 0;
    gs_err_t err;

    err = git_capture(args, PROC_STDERR_NULL, top, &code);
    if (err != GS_OK) {
        goto exit;
    }
    if (code != 0) {
        err = GS_ERR_NOT_REPO;
        goto exit;
    }
    str_trim_eol(top);
    if (top->len == 0U) {
        err = GS_ERR_NOT_REPO; /* es. dentro .git: nessun working tree */
    }

exit:
    return err;
}
