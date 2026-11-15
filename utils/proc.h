/*
 * proc.h - processi figli senza shell (D-017, D-067).
 *
 * Con sys, unico modulo con codice diverso per piattaforma: proc_win.c
 * (CreateProcess) e proc_posix.c (fork + exec). proc.c contiene la parte
 * comune. Gli argomenti passano al figlio cosi' come sono: nessuna shell
 * li interpreta, quindi path con spazi o caratteri speciali non richiedono
 * quoting da parte del chiamante.
 *
 * Stdin del figlio: vuoto, oppure scritto dal padre (PROC_STDIN_PIPE).
 * Stdout: letto dal padre. Stderr: quello del padre, oppure scartato
 * (PROC_STDERR_NULL). Testi in UTF-8 su tutte le piattaforme.
 *
 * Si possono tenere aperti piu' processi insieme: i descrittori del padre
 * non passano ai figli avviati dopo.
 */
#ifndef GS_PROC_H
#define GS_PROC_H

#include <stdint.h>
#include <stdio.h>

#include "err.h"
#include "str.h"

typedef struct {
    FILE *in;        /* stdin del figlio (solo con PROC_STDIN_PIPE) */
    FILE *out;       /* stdout del figlio */
    intptr_t handle; /* HANDLE del processo (Windows) o pid (POSIX) */
} Proc_t;

#define PROC_STDERR_NULL 0x1U /* scarta lo stderr del figlio */
#define PROC_STDIN_PIPE  0x2U /* il padre scrive lo stdin del figlio */
/* Toglie dall'ambiente del figlio le variabili che legano git a una repo
 * (proc_repo_env). Negli hook git le esporta per la repo corrente: un
 * comando diretto a un'altra repo non deve vederle (A-27, D-071). */
#define PROC_CLEAN_ENV   0x4U

/* Variabili tolte da PROC_CLEAN_ENV; l'elenco termina con NULL. */
extern const char *const proc_repo_env[];

/*
 * Avvia argv[0] con gli argomenti argv[1..] (argv termina con NULL).
 * Se argv[0] contiene un separatore di path e' usato cosi' com'e',
 * altrimenti e' cercato nel PATH.
 *
 * Con PROC_STDIN_PIPE, per un dialogo domanda/risposta si scrive una
 * domanda, si fa fflush(p->in) e si legge la risposta: scrivere tutto
 * prima di leggere puo' bloccare entrambi i processi a pipe piene.
 */
eGsErr_t proc_start(Proc_t *p, const char *const argv[], unsigned flags);

/*
 * Legge dallo stdout del figlio fino al delimitatore (escluso) o alla
 * fine. Ritorna 1 se ha letto un record, 0 a fine output.
 */
int proc_read(Proc_t *p, int delim, Str_t *rec);

/* Chiude lo stdin del figlio, che riceve la fine dell'input. */
void proc_close_in(Proc_t *p);

/*
 * Chiude stdin e stdout (un figlio che sta ancora scrivendo termina) e
 * attende la fine del processo. exit_code riceve il codice di uscita.
 */
eGsErr_t proc_finish(Proc_t *p, int *exit_code);

/* Legge tutto lo stdout in `out`, poi come proc_finish. */
eGsErr_t proc_collect(Proc_t *p, Str_t *out, int *exit_code);

/* proc_start + proc_collect. */
eGsErr_t proc_capture(const char *const argv[], unsigned flags, Str_t *out,
                      int *exit_code);

#endif /* GS_PROC_H */
