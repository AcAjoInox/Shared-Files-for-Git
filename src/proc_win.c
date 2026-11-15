/*
 * proc_win.c - processi figli su Windows (CreateProcessW).
 *
 * Internamente le stringhe sono UTF-8; verso le API di Windows si passa
 * per UTF-16 (funzioni ...W). La riga di comando del figlio e' costruita
 * con le regole di quoting di CommandLineToArgvW: nessuna shell in mezzo.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <fcntl.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>

#include "mem.h"
#include "proc.h"

/* UTF-8 -> UTF-16, allocata con xmalloc. */
static wchar_t *utf8_to_wide(const char *s)
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

/* UTF-16 -> UTF-8, allocata con xmalloc. */
static char *wide_to_utf8(const wchar_t *w)
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

/*
 * Aggiunge un argomento alla riga di comando con le regole di
 * CommandLineToArgvW: tra virgolette se contiene spazi o virgolette;
 * i backslash si raddoppiano solo davanti a una virgoletta.
 */
static void append_arg(Str_t *cmd, const char *arg)
{
    size_t bs = 0U;
    const char *c;

    if ((arg[0] != '\0') && (strpbrk(arg, " \t\n\v\"") == NULL)) {
        str_add(cmd, arg);
        goto exit;
    }
    str_addch(cmd, '"');
    for (c = arg; *c != '\0'; c++) {
        if (*c == '\\') {
            bs++;
        } else if (*c == '"') {
            for (; bs > 0U; bs--) {
                str_add(cmd, "\\\\");
            }
            str_add(cmd, "\\\"");
        } else {
            for (; bs > 0U; bs--) {
                str_addch(cmd, '\\');
            }
            str_addch(cmd, *c);
        }
    }
    for (; bs > 0U; bs--) {
        str_add(cmd, "\\\\"); /* prima della virgoletta di chiusura */
    }
    str_addch(cmd, '"');

exit:
    return;
}

/*
 * Cerca `name` + ".exe" nelle cartelle del PATH. Non usa la ricerca di
 * CreateProcess, che guarda prima la cartella corrente: un git.exe dentro
 * una repo verrebbe eseguito al posto di quello vero.
 */
static wchar_t *find_in_path(const char *name)
{
    Str_t file = STR_INIT;
    wchar_t *wfile;
    wchar_t *found = NULL;
    DWORD n;

    str_add(&file, name);
    if (strchr(name, '.') == NULL) {
        str_add(&file, ".exe");
    }
    wfile = utf8_to_wide(str_cstr(&file));
    n = SearchPathW(_wgetenv(L"PATH"), wfile, NULL, 0, NULL, NULL);
    if (n > 0U) {
        found = xmalloc((size_t)n * sizeof *found);
        if (SearchPathW(_wgetenv(L"PATH"), wfile, NULL, n, found, NULL) == 0U) {
            free(found);
            found = NULL;
        }
    }
    free(wfile);
    str_free(&file);
    return found;
}

/* Copia ereditabile di un handle standard; NUL se non e' valido. */
static HANDLE inheritable(HANDLE h)
{
    HANDLE dup = INVALID_HANDLE_VALUE;
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };

    if ((h == NULL) || (h == INVALID_HANDLE_VALUE) ||
        !DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &dup, 0,
                         TRUE, DUPLICATE_SAME_ACCESS)) {
        dup = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                          OPEN_EXISTING, 0, NULL);
    }
    return dup;
}

static HANDLE open_null(void)
{
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };

    return CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING,
                       0, NULL);
}

eGsErr_t proc_start(Proc_t *p, const char *const argv[], unsigned flags)
{
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    HANDLE rd = NULL;
    HANDLE wr = NULL;
    HANDLE in = INVALID_HANDLE_VALUE;
    HANDLE errh = INVALID_HANDLE_VALUE;
    Str_t cmd = STR_INIT;
    wchar_t *wcmd = NULL;
    wchar_t *wapp = NULL;
    size_t i;
    int fd;
    eGsErr_t err = GS_ERR_PROC;

    p->out = NULL;
    p->handle = 0;

    if (strpbrk(argv[0], "/\\") != NULL) {
        wapp = utf8_to_wide(argv[0]);
    } else {
        wapp = find_in_path(argv[0]);
    }
    if (wapp == NULL) {
        goto exit;
    }
    for (i = 0U; argv[i] != NULL; i++) {
        if (i > 0U) {
            str_addch(&cmd, ' ');
        }
        append_arg(&cmd, argv[i]);
    }
    wcmd = utf8_to_wide(str_cstr(&cmd));

    if (!CreatePipe(&rd, &wr, &sa, 0)) {
        goto exit;
    }
    (void)SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0); /* resta nostro */
    in = open_null();
    errh = ((flags & PROC_STDERR_NULL) != 0U)
               ? open_null()
               : inheritable(GetStdHandle(STD_ERROR_HANDLE));

    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in;
    si.hStdOutput = wr;
    si.hStdError = errh;
    if (!CreateProcessW(wapp, wcmd, NULL, NULL, TRUE, 0, NULL, NULL, &si,
                        &pi)) {
        goto exit;
    }
    (void)CloseHandle(pi.hThread);

    fd = _open_osfhandle((intptr_t)rd, _O_RDONLY | _O_BINARY);
    if (fd < 0) {
        (void)TerminateProcess(pi.hProcess, 1U);
        (void)CloseHandle(pi.hProcess);
        goto exit;
    }
    rd = NULL; /* ora appartiene a fd */
    p->out = _fdopen(fd, "rb");
    if (p->out == NULL) {
        (void)_close(fd);
        (void)TerminateProcess(pi.hProcess, 1U);
        (void)CloseHandle(pi.hProcess);
        goto exit;
    }
    p->handle = (intptr_t)pi.hProcess;
    err = GS_OK;

exit:
    /* Gli handle passati al figlio non servono piu' al padre: chiudere wr
     * e' cio' che fa arrivare la fine dell'output quando il figlio esce. */
    if (wr != NULL) {
        (void)CloseHandle(wr);
    }
    if (rd != NULL) {
        (void)CloseHandle(rd);
    }
    if (in != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(in);
    }
    if (errh != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(errh);
    }
    free(wcmd);
    free(wapp);
    str_free(&cmd);
    return err;
}

eGsErr_t proc_finish(Proc_t *p, int *exit_code)
{
    HANDLE h = (HANDLE)p->handle;
    DWORD code = 0U;
    eGsErr_t err = GS_ERR_PROC;

    (void)fclose(p->out);
    p->out = NULL;
    if (WaitForSingleObject(h, INFINITE) != WAIT_OBJECT_0) {
        goto exit;
    }
    if (!GetExitCodeProcess(h, &code)) {
        goto exit;
    }
    *exit_code = (int)code;
    err = GS_OK;

exit:
    (void)CloseHandle(h);
    return err;
}

void proc_utf8_args(int *argc, char ***argv)
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
        args[i] = wide_to_utf8(wargv[i]);
    }
    args[n] = NULL;
    (void)LocalFree(wargv);
    *argc = n;
    *argv = args;

exit:
    return;
}
