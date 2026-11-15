/*
 * proc.c - parte dei processi comune a tutte le piattaforme.
 */
#include "proc.h"

/* Come "git rev-parse --local-env-vars", senza quelle di configurazione. */
const char *const proc_repo_env[] = {
    "GIT_DIR",
    "GIT_WORK_TREE",
    "GIT_IMPLICIT_WORK_TREE",
    "GIT_INDEX_FILE",
    "GIT_OBJECT_DIRECTORY",
    "GIT_ALTERNATE_OBJECT_DIRECTORIES",
    "GIT_COMMON_DIR",
    "GIT_PREFIX",
    "GIT_SHALLOW_FILE",
    "GIT_GRAFT_FILE",
    NULL
};

int proc_read(Proc_t *p, int delim, Str_t *rec)
{
    int c;
    int got = 0;

    str_reset(rec);
    while ((c = getc(p->out)) != EOF) {
        got = 1;
        if (c == delim) {
            break;
        }
        str_addch(rec, (char)c);
    }
    return got;
}

void proc_close_in(Proc_t *p)
{
    if (p->in != NULL) {
        (void)fclose(p->in);
        p->in = NULL;
    }
}

eGsErr_t proc_collect(Proc_t *p, Str_t *out, int *exit_code)
{
    char chunk[4096];
    size_t n;

    str_reset(out);
    while ((n = fread(chunk, 1U, sizeof chunk, p->out)) > 0U) {
        str_addn(out, chunk, n);
    }
    return proc_finish(p, exit_code);
}

eGsErr_t proc_capture(const char *const argv[], unsigned flags, Str_t *out,
                      int *exit_code)
{
    Proc_t p;
    eGsErr_t err;

    err = proc_start(&p, argv, flags);
    if (err == GS_OK) {
        err = proc_collect(&p, out, exit_code);
    }
    return err;
}
