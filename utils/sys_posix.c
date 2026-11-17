/*
 * sys_posix.c - servizi del sistema operativo su Linux.
 */
#define _POSIX_C_SOURCE 200809L

#include "sys.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "mem.h"

char *sys_getenv(const char *name)
{
    const char *value = getenv(name);

    return (value != NULL) ? xstrdup(value) : NULL;
}

void sys_init(int *argc, char ***argv)
{
    (void)argc;
    (void)argv;
    (void)signal(SIGPIPE, SIG_IGN);
}

eGsErr_t sys_random(uint8_t *buf, size_t n)
{
    size_t got = 0U;
    ssize_t r;
    int stop = 0;
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    eGsErr_t err = GS_ERR_SYS;

    if (fd < 0) {
        goto exit;
    }
    while ((got < n) && (stop == 0)) {
        r = read(fd, buf + got, n - got);
        if (r > 0) {
            got += (size_t)r;
        } else if ((r < 0) && (errno == EINTR)) {
            /* interrotto da un segnale: si riprova */
        } else {
            stop = 1;
        }
    }
    (void)close(fd);
    if (got == n) {
        err = GS_OK;
    }

exit:
    return err;
}

FILE *sys_fopen(const char *path, const char *mode)
{
    return fopen(path, mode);
}

int sys_remove(const char *path)
{
    return remove(path);
}

int sys_make_executable(const char *path)
{
    struct stat st;
    int rc = -1;

    if (stat(path, &st) == 0) {
        rc = chmod(path, st.st_mode | S_IXUSR | S_IXGRP | S_IXOTH);
    }
    return rc;
}

eGsErr_t sys_abs_path(const char *path, Str_t *out)
{
    const char *slash = strrchr(path, '/');
    const char *name = (slash != NULL) ? (slash + 1) : path;
    Str_t dir = STR_INIT;
    char cwd[PATH_MAX];
    char *real;
    eGsErr_t err = GS_OK;

    if ((*name == '\0') || (strcmp(name, ".") == 0) || (strcmp(name, "..") == 0)) {
        str_add(&dir, path);
        name = "";
    } else if (slash == NULL) {
        str_add(&dir, ".");
    } else if (slash == path) {
        str_add(&dir, "/");
    } else {
        str_addn(&dir, path, (size_t)(slash - path));
    }
    str_reset(out);
    real = realpath(str_cstr(&dir), NULL);
    if (real != NULL) {
        str_add(out, real);
        free(real);
    } else if (str_cstr(&dir)[0] == '/') {
        str_add(out, str_cstr(&dir));
    } else if (getcwd(cwd, sizeof cwd) != NULL) {
        str_addf(out, "%s/%s", cwd, str_cstr(&dir));
    } else {
        err = GS_ERR_SYS;
    }
    if ((err == GS_OK) && (*name != '\0')) {
        if ((out->len == 0U) || (out->buf[out->len - 1U] != '/')) {
            str_addch(out, '/');
        }
        str_add(out, name);
    }
    str_free(&dir);
    return err;
}

int sys_is_dir(const char *path)
{
    struct stat st;

    return (stat(path, &st) == 0) && S_ISDIR(st.st_mode);
}
