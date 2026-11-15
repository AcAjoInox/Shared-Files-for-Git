/*
 * sys_posix.c - servizi del sistema operativo su Linux.
 */
#define _POSIX_C_SOURCE 200809L

#include "sys.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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

int sys_is_dir(const char *path)
{
    struct stat st;

    return (stat(path, &st) == 0) && S_ISDIR(st.st_mode);
}
