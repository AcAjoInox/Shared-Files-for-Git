/*
 * proc_win.c - processi figli su Windows (CreateProcessW).
 *
 * Internamente le stringhe sono UTF-8; verso le API di Windows si passa
 * per UTF-16 (funzioni ...W). La riga di comando del figlio e' costruita
 * con le regole di quoting di CommandLineToArgvW: nessuna shell in mezzo.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <fcntl.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include "mem.h"
#include "proc.h"
#include "sys_win.h"

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
    wfile = win_utf8_to_wide(str_cstr(&file));
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

static HANDLE open_null(void)
{
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };

    return CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING,
                       0, NULL);
}

/* Copia ereditabile di un handle standard; NUL se non e' valido. */
static HANDLE inheritable(HANDLE h)
{
    HANDLE dup = INVALID_HANDLE_VALUE;

    if ((h == NULL) || (h == INVALID_HANDLE_VALUE) ||
        !DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &dup, 0,
                         TRUE, DUPLICATE_SAME_ACCESS)) {
        dup = open_null();
    }
    return dup;
}

/* 1 se la voce "NOME=valore" e' una delle proc_repo_env (nomi senza
 * distinzione di maiuscole, come in Windows). */
static int is_repo_var(const wchar_t *entry)
{
    const char *name;
    size_t i;
    size_t k;
    int match = 0;

    for (i = 0U; (proc_repo_env[i] != NULL) && (match == 0); i++) {
        name = proc_repo_env[i];
        k = 0U;
        while ((name[k] != '\0') &&
               (towupper(entry[k]) == (wint_t)(unsigned char)name[k])) {
            k++;
        }
        match = (name[k] == '\0') && (entry[k] == L'=');
    }
    return match;
}

/* Copia dell'ambiente senza le proc_repo_env, nel formato di
 * CreateProcessW: "A=1\0B=2\0\0". */
static wchar_t *clean_env_block(void)
{
    wchar_t *env = GetEnvironmentStringsW();
    const wchar_t *e;
    wchar_t *block = NULL;
    wchar_t *dst;
    size_t total = 0U;
    size_t len;

    if (env != NULL) {
        for (e = env; *e != L'\0'; e += wcslen(e) + 1U) {
            total += wcslen(e) + 1U;
        }
        block = xmalloc((total + 2U) * sizeof *block);
        dst = block;
        for (e = env; *e != L'\0'; e += len + 1U) {
            len = wcslen(e);
            if (!is_repo_var(e)) {
                memcpy(dst, e, (len + 1U) * sizeof *e);
                dst += len + 1U;
            }
        }
        dst[0] = L'\0';
        dst[1] = L'\0'; /* ambiente vuoto: servono due terminatori */
        (void)FreeEnvironmentStringsW(env);
    }
    return block;
}

/* HANDLE -> FILE *. Se riesce, l'handle passa al FILE e *h diventa NULL. */
static FILE *wrap_handle(HANDLE *h, int oflag, const char *mode)
{
    FILE *f = NULL;
    int fd = _open_osfhandle((intptr_t)*h, oflag | _O_BINARY);

    if (fd >= 0) {
        *h = NULL;
        f = _fdopen(fd, mode);
        if (f == NULL) {
            (void)_close(fd);
        }
    }
    return f;
}

static void close_handle(HANDLE *h)
{
    if ((*h != NULL) && (*h != INVALID_HANDLE_VALUE)) {
        (void)CloseHandle(*h);
    }
    *h = NULL;
}

eGsErr_t proc_start(Proc_t *p, const char *const argv[], unsigned flags)
{
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    HANDLE out_rd = NULL;
    HANDLE out_wr = NULL;
    HANDLE in_rd = NULL;
    HANDLE in_wr = NULL;
    HANDLE errh = NULL;
    HANDLE child = NULL;
    Str_t cmd = STR_INIT;
    wchar_t *wcmd = NULL;
    wchar_t *wapp = NULL;
    wchar_t *wenv = NULL;
    size_t i;
    eGsErr_t err = GS_ERR_PROC;

    p->in = NULL;
    p->out = NULL;
    p->handle = 0;
    p->reaped = 0;
    p->status = 0;

    if (strpbrk(argv[0], "/\\") != NULL) {
        wapp = win_utf8_to_wide(argv[0]);
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
    wcmd = win_utf8_to_wide(str_cstr(&cmd));

    /* Le estremita' del padre non sono ereditabili: un figlio avviato dopo
     * non le riceve, e chiudere le nostre basta a dare la fine dei dati. */
    if (!CreatePipe(&out_rd, &out_wr, &sa, 65536U)) { /* vedi proc_exited */
        goto exit;
    }
    (void)SetHandleInformation(out_rd, HANDLE_FLAG_INHERIT, 0);
    if ((flags & PROC_STDIN_PIPE) != 0U) {
        if (!CreatePipe(&in_rd, &in_wr, &sa, 0)) {
            goto exit;
        }
        (void)SetHandleInformation(in_wr, HANDLE_FLAG_INHERIT, 0);
    } else {
        in_rd = open_null();
    }
    if ((flags & PROC_STDERR_OUT) != 0U) {
        errh = NULL; /* hStdError = out_wr, chiusa sotto */
    } else if ((flags & PROC_STDERR_NULL) != 0U) {
        errh = open_null();
    } else {
        errh = inheritable(GetStdHandle(STD_ERROR_HANDLE));
    }

    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in_rd;
    si.hStdOutput = out_wr;
    si.hStdError = ((flags & PROC_STDERR_OUT) != 0U) ? out_wr : errh;
    if ((flags & PROC_CLEAN_ENV) != 0U) {
        wenv = clean_env_block();
    }
    if (!CreateProcessW(wapp, wcmd, NULL, NULL, TRUE,
                        (wenv != NULL) ? CREATE_UNICODE_ENVIRONMENT : 0U, wenv,
                        NULL, &si, &pi)) {
        goto exit;
    }
    (void)CloseHandle(pi.hThread);
    child = pi.hProcess;

    p->out = wrap_handle(&out_rd, _O_RDONLY, "rb");
    if (p->out == NULL) {
        goto exit;
    }
    if (in_wr != NULL) {
        p->in = wrap_handle(&in_wr, _O_WRONLY, "wb");
        if (p->in == NULL) {
            goto exit;
        }
    }
    p->handle = (intptr_t)child;
    child = NULL;
    err = GS_OK;

exit:
    if (child != NULL) { /* avviato ma non collegato: lo si ferma */
        (void)TerminateProcess(child, 1U);
        (void)CloseHandle(child);
        if (p->out != NULL) {
            (void)fclose(p->out);
            p->out = NULL;
        }
    }
    /* Le estremita' del figlio non servono piu' al padre. */
    close_handle(&out_wr);
    close_handle(&in_rd);
    close_handle(&errh);
    close_handle(&out_rd);
    close_handle(&in_wr);
    free(wenv);
    free(wcmd);
    free(wapp);
    str_free(&cmd);
    return err;
}

int proc_exited(Proc_t *p)
{
    return WaitForSingleObject((HANDLE)p->handle, 0U) == WAIT_OBJECT_0;
}

eGsErr_t proc_finish(Proc_t *p, int *exit_code)
{
    HANDLE h = (HANDLE)p->handle;
    DWORD code = 0U;
    eGsErr_t err = GS_ERR_PROC;

    proc_close_in(p);
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
