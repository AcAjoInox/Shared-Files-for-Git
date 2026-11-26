/*
 * sys_posix.c - servizi del sistema operativo su Linux.
 */
#define _POSIX_C_SOURCE 200809L

#include "sys.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "mem.h"

char *sys_getenv(const char *name)
{
    const char *value = getenv(name);

    return (value != NULL) ? xstrdup(value) : NULL;
}

int sys_setenv(const char *name, const char *value)
{
    return setenv(name, value, 1);
}

int sys_stdin_is_tty(void)
{
    return isatty(STDIN_FILENO) != 0;
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

FILE *sys_fopen_excl(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
    FILE *f = NULL;

    if (fd >= 0) {
        f = fdopen(fd, "wb");
        if (f == NULL) {
            (void)close(fd);
        }
    }
    return f;
}

void sys_unsetenv(const char *name)
{
    (void)unsetenv(name);
}

void sys_sleep_ms(unsigned ms)
{
    struct timespec ts;

    ts.tv_sec = (time_t)(ms / 1000U);
    ts.tv_nsec = (long)(ms % 1000U) * 1000000L;
    (void)nanosleep(&ts, NULL);
}

void sys_stdout_binary(void)
{
    /* POSIX: nessun modo testo */
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

int sys_copy_mode(const char *from, const char *to)
{
    struct stat st;
    int rc = -1;

    if (stat(from, &st) == 0) {
        rc = chmod(to, st.st_mode & 07777);
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

int sys_chdir(const char *path)
{
    return chdir(path);
}

eGsErr_t sys_self_exe(Str_t *out)
{
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1U);
    eGsErr_t err = GS_ERR_SYS;

    /* Solo Linux; altrove il chiamante cerca l'eseguibile nel PATH. */
    if (n > 0) {
        buf[n] = '\0';
        str_reset(out);
        str_add(out, buf);
        err = GS_OK;
    }
    return err;
}

int sys_mkdir(const char *path)
{
    return ((mkdir(path, 0777) == 0) || sys_is_dir(path)) ? 0 : -1;
}

int sys_rmdir(const char *path)
{
    return rmdir(path);
}

int sys_rename(const char *from, const char *to)
{
    return rename(from, to);
}

eGsErr_t sys_list_dir(const char *path, Str_t *names)
{
    DIR *d = opendir(path);
    const struct dirent *de;
    eGsErr_t err = GS_ERR_SYS;

    if (d != NULL) {
        for (de = readdir(d); de != NULL; de = readdir(d)) {
            if ((strcmp(de->d_name, ".") != 0) && (strcmp(de->d_name, "..") != 0)) {
                str_addz(names, de->d_name);
            }
        }
        (void)closedir(d);
        err = GS_OK;
    }
    return err;
}
