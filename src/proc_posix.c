/*
 * proc_posix.c - processi figli su Linux (fork + exec).
 */
#define _POSIX_C_SOURCE 200809L /* fork, pipe, fdopen, waitpid con -std=c11 */

#include "proc.h"

#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Nel figlio, dopo fork: collega i descrittori e lancia il programma. */
static _Noreturn void child_exec(const char *const argv[], unsigned flags,
                                 int fd_out)
{
    int null_fd = open("/dev/null", O_RDWR);

    if (null_fd >= 0) {
        (void)dup2(null_fd, STDIN_FILENO);
        if ((flags & PROC_STDERR_NULL) != 0U) {
            (void)dup2(null_fd, STDERR_FILENO);
        }
        (void)close(null_fd);
    }
    (void)dup2(fd_out, STDOUT_FILENO);
    (void)close(fd_out);

    /* execv/execvp non modificano argv: il cast toglie solo il const. */
    if (strchr(argv[0], '/') != NULL) {
        (void)execv(argv[0], (char *const *)argv);
    } else {
        (void)execvp(argv[0], (char *const *)argv);
    }
    _exit(127); /* come la shell quando il comando non si trova */
}

gs_err_t proc_start(Proc_t *p, const char *const argv[], unsigned flags)
{
    int fds[2];
    pid_t pid;
    gs_err_t err = GS_ERR_PROC;

    p->out = NULL;
    p->handle = 0;
    if (pipe(fds) != 0) {
        goto exit;
    }
    pid = fork();
    if (pid < 0) {
        (void)close(fds[0]);
        (void)close(fds[1]);
        goto exit;
    }
    if (pid == 0) {
        (void)close(fds[0]);
        child_exec(argv, flags, fds[1]);
    }
    (void)close(fds[1]);
    p->out = fdopen(fds[0], "r");
    if (p->out == NULL) {
        (void)close(fds[0]);
        (void)waitpid(pid, NULL, 0);
        goto exit;
    }
    p->handle = (intptr_t)pid;
    err = GS_OK;

exit:
    return err;
}

gs_err_t proc_finish(Proc_t *p, int *exit_code)
{
    int status = 0;
    gs_err_t err = GS_ERR_PROC;

    (void)fclose(p->out);
    p->out = NULL;
    if (waitpid((pid_t)p->handle, &status, 0) < 0) {
        goto exit;
    }
    if (WIFEXITED(status)) {
        *exit_code = WEXITSTATUS(status);
    } else {
        *exit_code = 128 + WTERMSIG(status); /* come la shell */
    }
    if (*exit_code == 127) {
        goto exit; /* programma non trovato (child_exec) */
    }
    err = GS_OK;

exit:
    return err;
}

void proc_utf8_args(int *argc, char ***argv)
{
    (void)argc;
    (void)argv;
}
