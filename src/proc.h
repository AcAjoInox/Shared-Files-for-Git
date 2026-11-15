/*
 * proc.h - processi figli senza shell (D-017, D-067).
 *
 * Unico modulo con codice diverso per piattaforma: proc_win.c
 * (CreateProcess) e proc_posix.c (fork + exec). proc.c contiene la parte
 * comune. Gli argomenti passano al figlio cosi' come sono: nessuna shell
 * li interpreta, quindi path con spazi o caratteri speciali non richiedono
 * quoting da parte del chiamante.
 *
 * Il figlio riceve stdin vuoto; il suo stdout e' letto dal padre; il suo
 * stderr va su quello del padre oppure viene scartato (PROC_STDERR_NULL).
 * Testi in UTF-8 su tutte le piattaforme.
 */
#ifndef GS_PROC_H
#define GS_PROC_H

#include <stdint.h>
#include <stdio.h>

#include "err.h"
#include "str.h"

typedef struct {
    FILE *out;       /* stdout del figlio */
    intptr_t handle; /* HANDLE del processo (Windows) o pid (POSIX) */
} Proc_t;

#define PROC_STDERR_NULL 0x1U /* scarta lo stderr del figlio */

/*
 * Avvia argv[0] con gli argomenti argv[1..] (argv termina con NULL).
 * Se argv[0] contiene un separatore di path e' usato cosi' com'e',
 * altrimenti e' cercato nel PATH.
 */
gs_err_t proc_start(Proc_t *p, const char *const argv[], unsigned flags);

/*
 * Legge dallo stdout del figlio fino al delimitatore (escluso) o alla
 * fine. Ritorna 1 se ha letto un record, 0 a fine output.
 */
int proc_read(Proc_t *p, int delim, Str_t *rec);

/*
 * Chiude lo stdout (un figlio che sta ancora scrivendo termina) e attende
 * la fine del processo. exit_code riceve il codice di uscita del figlio.
 */
gs_err_t proc_finish(Proc_t *p, int *exit_code);

/* Avvia, legge tutto lo stdout in `out` e attende la fine. */
gs_err_t proc_capture(const char *const argv[], unsigned flags, Str_t *out,
                      int *exit_code);

/*
 * Converte gli argomenti di main in UTF-8 (su Windows main li riceve nella
 * code page di sistema). Su POSIX non fa nulla.
 */
void proc_utf8_args(int *argc, char ***argv);

#endif /* GS_PROC_H */
