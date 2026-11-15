/*
 * path.c - operazioni sui path come stringhe.
 */
#include "path.h"

#include <string.h>

static int is_sep(char c)
{
    return (c == '/') || (c == '\\');
}

int path_is_absolute(const char *p)
{
    return is_sep(p[0]) || ((p[0] != '\0') && (p[1] == ':'));
}

void path_split(const char *p, Str_t *dir, const char **name)
{
    const char *last = NULL;
    const char *c;

    for (c = p; *c != '\0'; c++) {
        if (is_sep(*c)) {
            last = c;
        }
    }
    str_reset(dir);
    if (last == NULL) {
        str_add(dir, ".");
        *name = p;
    } else if (last == p) {
        str_add(dir, "/");
        *name = last + 1;
    } else {
        str_addn(dir, p, (size_t)(last - p));
        *name = last + 1;
    }
}

int path_join_norm(const char *base, const char *rel, Str_t *out)
{
    Str_t all = STR_INIT;
    const char *c;
    const char *start;
    const char *slash;
    size_t n;
    int ok = 1;

    str_reset(out);
    str_add(&all, base);
    str_addch(&all, '/');
    str_add(&all, rel);
    c = str_cstr(&all);
    while (ok && (*c != '\0')) {
        while (is_sep(*c)) {
            c++;
        }
        start = c;
        while ((*c != '\0') && !is_sep(*c)) {
            c++;
        }
        n = (size_t)(c - start);
        if ((n == 0U) || ((n == 1U) && (start[0] == '.'))) {
            /* separatori doppi o ".": niente */
        } else if ((n == 2U) && (start[0] == '.') && (start[1] == '.')) {
            if (out->len == 0U) {
                ok = 0; /* sopra la base */
            } else {
                slash = strrchr(out->buf, '/');
                str_setlen(out, (slash != NULL) ? (size_t)(slash - out->buf) : 0U);
            }
        } else {
            if (out->len > 0U) {
                str_addch(out, '/');
            }
            str_addn(out, start, n);
        }
    }
    if (out->len == 0U) {
        ok = 0;
    }
    str_free(&all);
    return ok;
}
