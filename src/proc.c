/*
 * proc.c - parte dei processi comune a tutte le piattaforme.
 */
#include "proc.h"

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

gs_err_t proc_capture(const char *const argv[], unsigned flags, Str_t *out,
                      int *exit_code)
{
    Proc_t p;
    char chunk[4096];
    size_t n;
    gs_err_t err;

    err = proc_start(&p, argv, flags);
    if (err != GS_OK) {
        goto exit;
    }
    str_reset(out);
    while ((n = fread(chunk, 1U, sizeof chunk, p.out)) > 0U) {
        str_addn(out, chunk, n);
    }
    err = proc_finish(&p, exit_code);

exit:
    return err;
}
