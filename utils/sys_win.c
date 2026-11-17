/*
 * sys_win.c - servizi del sistema operativo su Windows.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "mem.h"
#include "sys.h"
#include "sys_win.h"

wchar_t *win_utf8_to_wide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = xmalloc((size_t)((n > 0) ? n : 1) * sizeof *w);

    if (n <= 0) {
        w[0] = L'\0';
    } else {
        (void)MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    }
    return w;
}

char *win_wide_to_utf8(const wchar_t *w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *s = xmalloc((size_t)((n > 0) ? n : 1));

    if (n <= 0) {
        s[0] = '\0';
    } else {
        (void)WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    }
    return s;
}

void sys_init(int *argc, char ***argv)
{
    int n = 0;
    int i;
    wchar_t **wargv = CommandLineToArgvW(GetCommandLineW(), &n);
    char **args;

    if (wargv == NULL) {
        goto exit; /* si tengono gli argomenti originali */
    }
    args = xmalloc(((size_t)n + 1U) * sizeof *args);
    for (i = 0; i < n; i++) {
        args[i] = win_wide_to_utf8(wargv[i]);
    }
    args[n] = NULL;
    (void)LocalFree(wargv);
    *argc = n;
    *argv = args;

exit:
    return;
}

eGsErr_t sys_random(uint8_t *buf, size_t n)
{
    eGsErr_t err = GS_ERR_SYS;

    if ((n <= (size_t)ULONG_MAX) &&
        BCRYPT_SUCCESS(BCryptGenRandom(NULL, buf, (ULONG)n,
                                       BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
        err = GS_OK;
    }
    return err;
}

FILE *sys_fopen(const char *path, const char *mode)
{
    wchar_t *wpath = win_utf8_to_wide(path);
    wchar_t *wmode = win_utf8_to_wide(mode);
    FILE *f = _wfopen(wpath, wmode);

    free(wpath);
    free(wmode);
    return f;
}

int sys_remove(const char *path)
{
    wchar_t *wpath = win_utf8_to_wide(path);
    int rc = _wremove(wpath);

    free(wpath);
    return rc;
}

int sys_make_executable(const char *path)
{
    (void)path; /* Git for Windows lancia gli hook con la propria sh */
    return 0;
}

/* Divide path in cartella e ultimo nome ("." e ".." restano cartella). */
static void split_last(const char *path, Str_t *dir, Str_t *name)
{
    const char *last = NULL;
    const char *c;

    for (c = path; *c != '\0'; c++) {
        if ((*c == '/') || (*c == '\\')) {
            last = c;
        }
    }
    c = (last != NULL) ? (last + 1) : path;
    if ((*c == '\0') || (strcmp(c, ".") == 0) || (strcmp(c, "..") == 0)) {
        str_add(dir, path); /* nessun nome finale da staccare */
    } else if (last == NULL) {
        str_add(dir, ".");
        str_add(name, c);
    } else if ((last == path) || ((last == (path + 2)) && (path[1] == ':'))) {
        str_addn(dir, path, (size_t)(last - path) + 1U); /* "/" o "C:/" */
        str_add(name, c);
    } else {
        str_addn(dir, path, (size_t)(last - path));
        str_add(name, c);
    }
}

eGsErr_t sys_abs_path(const char *path, Str_t *out)
{
    Str_t dir = STR_INIT;
    Str_t name = STR_INIT;
    wchar_t *wdir;
    wchar_t *full = NULL;
    wchar_t *longp = NULL;
    char *u8;
    DWORD n;
    size_t i;
    eGsErr_t err = GS_ERR_SYS;

    split_last(path, &dir, &name);
    wdir = win_utf8_to_wide(str_cstr(&dir));
    n = GetFullPathNameW(wdir, 0U, NULL, NULL);
    if (n == 0U) {
        goto exit;
    }
    full = xmalloc((size_t)n * sizeof *full);
    if (GetFullPathNameW(wdir, n, full, NULL) == 0U) {
        goto exit;
    }
    /* Nomi 8.3 -> nomi lunghi, come li scrive git; solo se la cartella
     * esiste, altrimenti resta il nome completo. */
    n = GetLongPathNameW(full, NULL, 0U);
    if (n > 0U) {
        longp = xmalloc((size_t)n * sizeof *longp);
        if (GetLongPathNameW(full, longp, n) == 0U) {
            free(longp);
            longp = NULL;
        }
    }
    u8 = win_wide_to_utf8((longp != NULL) ? longp : full);
    str_reset(out);
    str_add(out, u8);
    free(u8);
    for (i = 0U; i < out->len; i++) {
        if (out->buf[i] == '\\') {
            out->buf[i] = '/';
        }
    }
    if ((out->len > 1U) && (out->buf[out->len - 1U] == '/') &&
        !((out->len == 3U) && (out->buf[1] == ':'))) {
        str_setlen(out, out->len - 1U);
    }
    if (out->len > 1U && (out->buf[1] == ':') && (out->buf[0] >= 'a') &&
        (out->buf[0] <= 'z')) {
        out->buf[0] = (char)(out->buf[0] - 'a' + 'A'); /* come git: "C:" */
    }
    if (name.len > 0U) {
        if (out->buf[out->len - 1U] != '/') {
            str_addch(out, '/');
        }
        str_add(out, str_cstr(&name));
    }
    err = GS_OK;

exit:
    free(longp);
    free(full);
    free(wdir);
    str_free(&name);
    str_free(&dir);
    return err;
}

int sys_is_dir(const char *path)
{
    wchar_t *wpath = win_utf8_to_wide(path);
    DWORD attr = GetFileAttributesW(wpath);

    free(wpath);
    return (attr != INVALID_FILE_ATTRIBUTES) &&
           ((attr & FILE_ATTRIBUTE_DIRECTORY) != 0U);
}
