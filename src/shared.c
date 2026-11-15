/*
 * shared.c - lettura di .gitshared.
 */
#include "shared.h"

#include <stdlib.h>
#include <string.h>

#include "bounds.h"
#include "git.h"
#include "mem.h"

void shared_list_free(SharedList_t *l)
{
    size_t i;

    for (i = 0U; i < l->n; i++) {
        str_free(&l->v[i].name);
        str_free(&l->v[i].id);
        str_free(&l->v[i].path);
    }
    free(l->v);
    l->v = NULL;
    l->n = 0U;
    l->cap = 0U;
}

/* Voce con quel nome; se manca, la aggiunge in fondo. */
static SharedEntry_t *entry_get(SharedList_t *l, const char *name,
                                size_t name_len)
{
    SharedEntry_t *e = NULL;
    size_t i;

    for (i = 0U; (i < l->n) && (e == NULL); i++) {
        if ((l->v[i].name.len == name_len) &&
            (memcmp(l->v[i].name.buf, name, name_len) == 0)) {
            e = &l->v[i];
        }
    }
    if (e == NULL) {
        if (l->n == l->cap) {
            l->cap = (l->cap == 0U) ? 8U : (l->cap * 2U);
            l->v = xrealloc(l->v, l->cap * sizeof *l->v);
        }
        e = &l->v[l->n];
        l->n++;
        str_init(&e->name);
        str_init(&e->id);
        str_init(&e->path);
        str_addn(&e->name, name, name_len);
    }
    return e;
}

/*
 * Un record di `git config -z --get-regexp` e' "chiave\nvalore".
 * La chiave e' "shared.<nome>.<campo>": il nome puo' contenere punti,
 * quindi il campo e' cio' che segue l'ultimo punto (§6.3).
 */
static void parse_record(SharedList_t *l, const Str_t *rec)
{
    static const char prefix[] = "shared.";
    const char *key = str_cstr(rec);
    const char *nl = strchr(key, '\n');
    const char *value = (nl != NULL) ? (nl + 1) : "";
    size_t key_len = (nl != NULL) ? (size_t)(nl - key) : strlen(key);
    const char *name = key + (sizeof prefix - 1U);
    const char *dot = NULL;
    const char *c;
    SharedEntry_t *e;

    if ((key_len <= (sizeof prefix - 1U)) ||
        (strncmp(key, prefix, sizeof prefix - 1U) != 0)) {
        goto exit;
    }
    for (c = name; c < (key + key_len); c++) {
        if (*c == '.') {
            dot = c;
        }
    }
    if ((dot == NULL) || (dot == name)) {
        goto exit; /* "shared.<campo>" senza nome: non e' una voce */
    }
    e = entry_get(l, name, (size_t)(dot - name));
    if (strncmp(dot + 1, "id\n", 3U) == 0) {
        str_reset(&e->id);
        str_add(&e->id, value);
    } else if (strncmp(dot + 1, "path\n", 5U) == 0) {
        str_reset(&e->path);
        str_add(&e->path, value);
    } else {
        /* altri campi (branch, url) non servono in M0 */
    }

exit:
    return;
}

gs_err_t shared_load(const char *top, SharedList_t *l)
{
    Str_t file = STR_INIT;
    Str_t rec = STR_INIT;
    const char *args[] = { "config", "-z", "-f", NULL, "--get-regexp",
                           "^shared\\.", NULL };
    Proc_t p;
    int code = 0;
    gs_err_t err;

    str_addf(&file, "%s/.gitshared", top);
    args[3] = str_cstr(&file);
    err = git_start(&p, args, 0U);
    if (err != GS_OK) {
        goto exit;
    }
    while (proc_read(&p, '\0', &rec) != 0) {
        parse_record(l, &rec);
    }
    err = proc_finish(&p, &code);
    if (err != GS_OK) {
        goto exit;
    }
    /* 0 = trovato, 1 = nessuna chiave o file assente, altro = file non
     * valido (verificato con git 2.43). */
    if (code > 1) {
        err = GS_ERR_FORMAT;
    }

exit:
    str_free(&rec);
    str_free(&file);
    return err;
}

int shared_entry_valid(const SharedEntry_t *e)
{
    const char *id = str_cstr(&e->id);
    const char *path = str_cstr(&e->path);
    size_t i;
    int ok = (e->id.len == GS_ID_HEX_LEN);

    for (i = 0U; ok && (i < GS_ID_HEX_LEN); i++) {
        /* id[i] != '\0': strchr troverebbe il terminatore */
        ok = (id[i] != '\0') && (strchr("0123456789abcdef", id[i]) != NULL);
    }
    /* Path relativo (D-003): non vuoto, senza '/' o '\' iniziale ne' "C:". */
    if (ok) {
        ok = (path[0] != '\0') && (path[0] != '/') && (path[0] != '\\') &&
             (path[1] != ':') && (e->path.len <= GS_PATH_MAX);
    }
    return ok;
}
