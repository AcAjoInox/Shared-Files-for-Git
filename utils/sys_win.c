/*
 * sys_win.c - servizi del sistema operativo su Windows.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>

#include <limits.h>
#include <stdlib.h>

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

int sys_is_dir(const char *path)
{
    wchar_t *wpath = win_utf8_to_wide(path);
    DWORD attr = GetFileAttributesW(wpath);

    free(wpath);
    return (attr != INVALID_FILE_ATTRIBUTES) &&
           ((attr & FILE_ATTRIBUTE_DIRECTORY) != 0U);
}
