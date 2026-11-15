/*
 * git.h - accesso a git tramite i suoi comandi (D-017).
 *
 * Il programma git lanciato e' quello della exec-path che git stesso ci
 * passa in GIT_EXEC_PATH: la stessa versione che ha lanciato l'estensione.
 * Senza GIT_EXEC_PATH (eseguibile lanciato a mano) si usa "git" dal PATH.
 */
#ifndef GS_GIT_H
#define GS_GIT_H

#include "err.h"
#include "proc.h"
#include "str.h"

/* Come proc_start/proc_capture, con "git" davanti agli argomenti.
 * args termina con NULL. */
eGsErr_t git_start(Proc_t *p, const char *const args[], unsigned flags);
eGsErr_t git_capture(const char *const args[], unsigned flags, Str_t *out,
                     int *exit_code);

/* Radice del working tree della repo corrente.
 * GS_ERR_NOT_REPO se la directory corrente non e' in un working tree. */
eGsErr_t git_toplevel(Str_t *top);

#endif /* GS_GIT_H */
