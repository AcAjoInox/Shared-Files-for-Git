/*
 * heirs.c - gli eredi dei sovrani: la linea di successione (D-114).
 */
#include "heirs.h"

#include <stdio.h>
#include <string.h>

#include "bounds.h"
#include "remote.h"

/* Una riga: due URL, un TAB e il fine riga. Controllata prima di leggere. */
#define HEIRS_LINE_MAX  (2UL * (unsigned long)GS_PATH_MAX + 2UL)
#define HEIRS_BLOB_MAX  ((unsigned long)HEIRS_LINES_MAX * HEIRS_LINE_MAX)
/* Livelli dell'albero al piu': ogni sovrano, e il creatore. */
#define HEIRS_DEPTH_MAX (GS_SOVEREIGN_MAX + 2U)

void heirs_free(Heirs_t *h)
{
    str_free(&h->pairs);
    h->n = 0U;
}

static void heirs_reset(Heirs_t *h)
{
    str_reset(&h->pairs);
    h->n = 0U;
}

/* Successiva coppia dopo *heir (NULL = la prima): 1 e i due puntatori. */
static int next_pair(const Heirs_t *h, const char **parent, const char **heir)
{
    const char *p = str_znext(&h->pairs, (*heir != NULL) ? *heir : NULL);
    const char *x = (p != NULL) ? str_znext(&h->pairs, p) : NULL;

    if ((p == NULL) || (x == NULL)) {
        return 0;
    }
    *parent = p;
    *heir = x;
    return 1;
}

/* 1 se url e' un sovrano di a. */
static int is_sovereign(const Auth_t *a, const char *url)
{
    const char *s;

    for (s = str_znext(&a->sovereigns, NULL); s != NULL;
         s = str_znext(&a->sovereigns, s)) {
        if (remote_same(s, url)) {
            return 1;
        }
    }
    return 0;
}

/* 1 se url ha regnato: il creatore o un sovrano. Solo loro hanno eredi. */
static int reigned(const Auth_t *a, const char *url)
{
    return remote_same(str_cstr(&a->creator), url) || is_sovereign(a, url);
}

/* Eredi di parent in h: quanti, e in first il primo (se non NULL). */
static unsigned children(const Heirs_t *h, const char *parent, const char **first)
{
    const char *p = NULL;
    const char *x = NULL;
    unsigned n = 0U;

    while (next_pair(h, &p, &x)) {
        if (remote_same(p, parent)) {
            if ((n == 0U) && (first != NULL)) {
                *first = x;
            }
            n++;
        }
    }
    return n;
}

int heirs_find(const Heirs_t *h, const char *url, const char **parent)
{
    const char *p = NULL;
    const char *x = NULL;

    /* Stretto (S-06): un erede si segue senza domande, un'altra repo che
     * gli somiglia no. */
    while (next_pair(h, &p, &x)) {
        if (remote_same_strict(x, url)) {
            if (parent != NULL) {
                *parent = p;
            }
            return 1;
        }
    }
    return 0;
}

void heirs_add(Heirs_t *h, const char *parent, const char *heir)
{
    str_addz(&h->pairs, parent);
    str_addz(&h->pairs, heir);
    h->n++;
}

/*
 * Una coppia letta entra se il padre ha regnato, ha ancora posto, l'erede
 * non c'e' gia', non e' il creatore ne' il padre stesso e non e' della
 * casata dell'altro erede. Un erede che si e' incoronato resta: e' il suo
 * posto nell'albero.
 */
static void add_checked(Heirs_t *h, const Auth_t *a, const char *parent,
                        const char *heir)
{
    const char *first = NULL;
    unsigned n;

    if (!reigned(a, parent) || remote_same(heir, parent) ||
        remote_same(heir, str_cstr(&a->creator)) || heirs_find(h, heir, NULL)) {
        return;
    }
    n = children(h, parent, &first);
    if ((n >= HEIRS_PER_PARENT) ||
        ((n == 1U) && remote_same_house(first, heir))) {
        return;
    }
    heirs_add(h, parent, heir);
}

eGsErr_t heirs_read(GitBatch_t *b, const Oid_t *blob, const Auth_t *a,
                    Heirs_t *h)
{
    Str_t text = STR_INIT;
    Str_t parent = STR_INIT;
    Str_t heir = STR_INIT;
    const char *line;
    const char *nl;
    const char *tab;
    size_t len;
    size_t lines = 0U;
    eGsErr_t err = GS_OK;

    heirs_reset(h);
    if ((blob == NULL) || (blob->len == 0U) || (a->creator.len == 0U)) {
        return GS_OK;
    }
    err = auth_blob_text(b, blob, HEIRS_BLOB_MAX, &text);
    for (line = str_cstr(&text); (err == GS_OK) && (text.len > 0U) && (line != NULL);
         line = (nl != NULL) ? (nl + 1) : NULL) {
        nl = strchr(line, '\n');
        len = (nl != NULL) ? (size_t)(nl - line) : strlen(line);
        tab = memchr(line, '\t', len);
        if ((tab == NULL) || (++lines > HEIRS_LINES_MAX)) {
            err = GS_ERR_FORMAT;
            break;
        }
        str_reset(&parent);
        str_addn(&parent, line, (size_t)(tab - line));
        str_reset(&heir);
        str_addn(&heir, tab + 1, len - (size_t)(tab - line) - 1U);
        if (!remote_url_ok(str_cstr(&parent)) || !remote_url_ok(str_cstr(&heir))) {
            err = GS_ERR_FORMAT; /* anche un secondo TAB: e' nell'URL */
        } else {
            add_checked(h, a, str_cstr(&parent), str_cstr(&heir));
        }
    }
    if (err != GS_OK) {
        heirs_reset(h);
    }
    str_free(&heir);
    str_free(&parent);
    str_free(&text);
    return err;
}

eGsErr_t heirs_of(GitBatch_t *b, const Oid_t *v, Auth_t *a, AuthBlobs_t *ab,
                  Heirs_t *h)
{
    AuthBlobs_t mine;
    AuthBlobs_t *p = (ab != NULL) ? ab : &mine;
    eGsErr_t err = auth_of(b, v, a, p);

    heirs_reset(h);
    if (err == GS_OK) {
        (void)heirs_read(b, &p->heirs, a, h);
    }
    return err;
}

eGsErr_t heirs_write(const Heirs_t *h, Oid_t *blob)
{
    static const char *const args[] = { "hash-object", "-w", "--stdin", NULL };
    const char *p = NULL;
    const char *x = NULL;
    Str_t out = STR_INIT;
    Proc_t proc;
    int code = 0;
    eGsErr_t err;

    blob->len = 0U;
    if (h->n == 0U) {
        return GS_OK;
    }
    err = git_start(&proc, args, PROC_STDIN_PIPE);
    if (err != GS_OK) {
        return err;
    }
    while (next_pair(h, &p, &x)) {
        if (fprintf(proc.in, "%s\t%s\n", p, x) < 0) {
            err = GS_ERR_GIT;
        }
    }
    proc_close_in(&proc);
    if (proc_collect(&proc, &out, &code) != GS_OK) {
        err = GS_ERR_PROC;
    }
    str_trim_eol(&out);
    if ((err == GS_OK) && ((code != 0) || !oid_from_hex(blob, out.buf, out.len))) {
        err = GS_ERR_GIT;
    }
    str_free(&out);
    return err;
}

int heirs_prune(Heirs_t *h, const Auth_t *a)
{
    Heirs_t keep = HEIRS_INIT;
    const char *p = NULL;
    const char *x = NULL;
    int dropped = 0;

    while (next_pair(h, &p, &x)) {
        if (reigned(a, p)) {
            heirs_add(&keep, p, x);
        } else {
            dropped = 1;
        }
    }
    if (dropped != 0) {
        heirs_reset(h);
        if (keep.n > 0U) {
            str_addn(&h->pairs, keep.pairs.buf, keep.pairs.len);
            h->n = keep.n;
        }
    }
    heirs_free(&keep);
    return dropped;
}

unsigned heirs_can_join(const Auth_t *a, const Heirs_t *h, const char *own)
{
    const char *authority = auth_authority(a);
    const char *first = NULL;
    unsigned n;

    if ((own == NULL) || !remote_url_ok(own) || (*authority == '\0') ||
        reigned(a, own) || heirs_find(h, own, NULL)) {
        return 0U;
    }
    n = children(h, authority, &first);
    if ((n >= HEIRS_PER_PARENT) || ((n == 1U) && remote_same_house(first, own))) {
        return 0U;
    }
    return n + 1U;
}

/* 1 se url e' gia' nell'elenco compatto list (in forma normale). */
static int listed(const Str_t *list, const char *url)
{
    const char *p;

    for (p = str_znext(list, NULL); p != NULL; p = str_znext(list, p)) {
        if (remote_same(p, url)) {
            return 1;
        }
    }
    return 0;
}

/* Il sottoalbero di node: ogni erede non ancora visto, seguito dal suo
 * sottoalbero se ha regnato. depth ferma un albero scritto male. */
static void visit(const Auth_t *a, const Heirs_t *h, const char *node,
                  Str_t *seen, Str_t *order, unsigned depth)
{
    const char *p = NULL;
    const char *x = NULL;

    if (depth > HEIRS_DEPTH_MAX) {
        return;
    }
    while (next_pair(h, &p, &x)) {
        if (remote_same(p, node) && !listed(seen, x)) {
            str_addz(seen, x);
            str_addz(order, x);
            if (reigned(a, x)) {
                visit(a, h, x, seen, order, depth + 1U);
            }
        }
    }
}

/* Il livello sopra cur: il suo padre se e' un erede, altrimenti il sovrano
 * che ha sostituito, o il creatore; NULL per il creatore. */
static const char *up_of(const Auth_t *a, const Heirs_t *h, const char *cur)
{
    const char *parent = NULL;
    const char *prev = NULL;
    const char *s;

    if (remote_same(cur, str_cstr(&a->creator))) {
        return NULL;
    }
    if (heirs_find(h, cur, &parent)) {
        return parent;
    }
    for (s = str_znext(&a->sovereigns, NULL); s != NULL;
         s = str_znext(&a->sovereigns, s)) {
        if (remote_same(s, cur)) {
            return (prev != NULL) ? prev : str_cstr(&a->creator);
        }
        prev = s;
    }
    return str_cstr(&a->creator);
}

void heirs_order(const Auth_t *a, const Heirs_t *h, Str_t *order)
{
    Str_t seen = STR_INIT; /* URL gia' nell'ordine, piu' autorita' e creatore */
    Str_t ups = STR_INIT;  /* livelli gia' saliti: un ciclo si ferma */
    const char *cur = auth_authority(a);
    const char *up;
    unsigned depth = 0U;

    str_reset(order);
    if (a->creator.len == 0U) {
        return;
    }
    str_addz(&seen, str_cstr(&a->creator));
    str_addz(&seen, cur);
    str_addz(&ups, cur);
    visit(a, h, cur, &seen, order, 0U);
    for (up = up_of(a, h, cur); (up != NULL) && (depth < HEIRS_DEPTH_MAX);
         up = up_of(a, h, cur), depth++) {
        if (listed(&ups, up)) {
            break;
        }
        str_addz(&ups, up);
        if (!listed(&seen, up)) {
            str_addz(&seen, up);
            str_addz(order, up); /* un sovrano di prima, non il creatore */
        }
        visit(a, h, up, &seen, order, 0U);
        cur = up;
    }
    str_free(&ups);
    str_free(&seen);
}

unsigned heirs_position(const Str_t *order, const char *url)
{
    const char *p;
    unsigned k = 1U;

    for (p = str_znext(order, NULL); p != NULL; p = str_znext(order, p), k++) {
        if (remote_same(p, url)) {
            return k;
        }
    }
    return 0U;
}
