/*
 * proc_posix.c - processi figli su Linux (fork + exec).
 */
#define _POSIX_C_SOURCE 200809L /* fork, pipe, fdopen, waitpid con -std=c11 */

#include "proc.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Il descrittore si chiude da solo all'exec: i figli avviati dopo non
 * ricevono le estremita' del padre. */
static void set_cloexec(int fd)
{
    int fl = fcntl(fd, F_GETFD);

    if (fl >= 0) {
        (void)fcntl(fd, F_SETFD, fl | FD_CLOEXEC);
    }
}

static void close_fd(int *fd)
{
    if (*fd >= 0) {
        (void)close(*fd);
    }
    *fd = -1;
}

/* Nel figlio, dopo fork: collega i descrittori e lancia il programma. */
static _Noreturn void child_exec(const char *const argv[], unsigned flags,
                                 int fd_in, int fd_out)
{
    int null_fd = open("/dev/null", O_RDWR);
    size_t i;

    if ((flags & PROC_CLEAN_ENV) != 0U) {
        for (i = 0U; proc_repo_env[i] != NULL; i++) {
            (void)unsetenv(proc_repo_env[i]); /* solo nel figlio */
        }
    }
    if (fd_in >= 0) {
        (void)dup2(fd_in, STDIN_FILENO);
        (void)close(fd_in);
    } else if (null_fd >= 0) {
        (void)dup2(null_fd, STDIN_FILENO);
    } else {
        /* senza /dev/null il figlio eredita lo stdin */
    }
    if (((flags & PROC_STDERR_NULL) != 0U) && (null_fd >= 0)) {
        (void)dup2(null_fd, STDERR_FILENO);
    }
    if (null_fd >= 0) {
        (void)close(null_fd);
    }
    (void)dup2(fd_out, STDOUT_FILENO);
    if ((flags & PROC_STDERR_OUT) != 0U) {
        (void)dup2(fd_out, STDERR_FILENO);
    }
    (void)close(fd_out);

    /* execv/execvp non modificano argv: il cast toglie solo il const. */
    if (strchr(argv[0], '/') != NULL) {
        (void)execv(argv[0], (char *const *)argv);
    } else {
        (void)execvp(argv[0], (char *const *)argv);
    }
    _exit(127); /* come la shell quando il comando non si trova */
}

eGsErr_t proc_start(Proc_t *p, const char *const argv[], unsigned flags)
{
    int out_fd[2] = { -1, -1 };
    int in_fd[2] = { -1, -1 };
    pid_t pid = -1;
    eGsErr_t err = GS_ERR_PROC;

    p->in = NULL;
    p->out = NULL;
    p->handle = 0;
    p->reaped = 0;
    p->status = 0;
    if (pipe(out_fd) != 0) {
        goto exit;
    }
    if (((flags & PROC_STDIN_PIPE) != 0U) && (pipe(in_fd) != 0)) {
        goto exit;
    }
    set_cloexec(out_fd[0]);
    if (in_fd[1] >= 0) {
        set_cloexec(in_fd[1]);
    }
    pid = fork();
    if (pid < 0) {
        goto exit;
    }
    if (pid == 0) {
        close_fd(&out_fd[0]);
        close_fd(&in_fd[1]);
        child_exec(argv, flags, in_fd[0], out_fd[1]);
    }
    close_fd(&out_fd[1]);
    close_fd(&in_fd[0]);

    p->out = fdopen(out_fd[0], "r");
    if (p->out == NULL) {
        goto exit;
    }
    out_fd[0] = -1; /* ora appartiene a p->out */
    if (in_fd[1] >= 0) {
        p->in = fdopen(in_fd[1], "w");
        if (p->in == NULL) {
            goto exit;
        }
        in_fd[1] = -1;
    }
    p->handle = (intptr_t)pid;
    err = GS_OK;

exit:
    close_fd(&out_fd[0]);
    close_fd(&out_fd[1]);
    close_fd(&in_fd[0]);
    close_fd(&in_fd[1]);
    if ((err != GS_OK) && (pid > 0)) { /* avviato ma non collegato */
        if (p->out != NULL) {
            (void)fclose(p->out);
            p->out = NULL;
        }
        (void)waitpid(pid, NULL, 0);
    }
    return err;
}

int proc_exited(Proc_t *p)
{
    int status = 0;

    if ((p->reaped == 0) && (waitpid((pid_t)p->handle, &status, WNOHANG) > 0)) {
        p->reaped = 1;
        p->status = status;
    }
    return p->reaped;
}

eGsErr_t proc_finish(Proc_t *p, int *exit_code)
{
    int status = 0;
    eGsErr_t err = GS_ERR_PROC;

    proc_close_in(p);
    (void)fclose(p->out);
    p->out = NULL;
    if (p->reaped != 0) {
        status = p->status; /* gia' raccolto da proc_exited */
    } else if (waitpid((pid_t)p->handle, &status, 0) < 0) {
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
