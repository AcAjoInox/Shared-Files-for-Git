/*
 * sys.h - servizi del sistema operativo che non riguardano i processi.
 *
 * Con proc, unico modulo con codice diverso per piattaforma (§6.2):
 * sys_win.c e sys_posix.c. Tutti i path sono in UTF-8.
 */
#ifndef GS_SYS_H
#define GS_SYS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "err.h"
#include "str.h"

/*
 * Da chiamare per prima in main. Windows: argomenti convertiti in UTF-8.
 * POSIX: SIGPIPE ignorato, cosi' scrivere a un figlio gia' terminato da'
 * un errore invece di terminare il programma.
 */
void sys_init(int *argc, char ***argv);

/* n byte dal generatore casuale del sistema operativo. */
eGsErr_t sys_random(uint8_t *buf, size_t n);

/*
 * Valore di una variabile d'ambiente in UTF-8, da liberare con free; NULL
 * se non c'e'. Windows: letta in UTF-16, non nella code page ANSI, che
 * non rappresenta tutti i path (reportIdea C-05).
 */
char *sys_getenv(const char *name);

FILE *sys_fopen(const char *path, const char *mode);
int sys_remove(const char *path);

/* 1 se il path esiste ed e' una cartella. */
int sys_is_dir(const char *path);

/*
 * Path assoluto con separatore '/', anche se l'ultimo elemento non esiste
 * piu' (es. una repo spostata): si risolve la cartella che lo contiene
 * (nomi lunghi su Windows, link simbolici su POSIX) e si aggiunge il nome.
 */
eGsErr_t sys_abs_path(const char *path, Str_t *out);

/* Bit di esecuzione per tutti (POSIX); su Windows non serve: 0 = riuscito. */
int sys_make_executable(const char *path);

#endif /* GS_SYS_H */
