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

/*
 * Scrive una variabile d'ambiente del processo, che i figli ereditano.
 * Windows: nell'ambiente del sistema operativo (SetEnvironmentVariableW),
 * da cui CreateProcess copia quello dei figli. 0 = riuscito.
 */
int sys_setenv(const char *name, const char *value);

FILE *sys_fopen(const char *path, const char *mode);
int sys_remove(const char *path);

/* 1 se il path esiste ed e' una cartella. */
int sys_is_dir(const char *path);

/* Crea una cartella (un solo livello). 0 se creata o se c'era gia'. */
int sys_mkdir(const char *path);

/* Toglie una cartella vuota. 0 = riuscito. */
int sys_rmdir(const char *path);

/*
 * Rinomina atomica nella stessa cartella; se `to` esiste lo sostituisce.
 * 0 = riuscito.
 */
int sys_rename(const char *from, const char *to);

/*
 * Nomi contenuti in una cartella, "." e ".." esclusi, in un elenco
 * compatto (str_addz), in ordine qualsiasi. GS_ERR_SYS se la cartella non
 * si apre (anche se non esiste).
 */
eGsErr_t sys_list_dir(const char *path, Str_t *names);

/*
 * Path assoluto con separatore '/', anche se l'ultimo elemento non esiste
 * piu' (es. una repo spostata): si risolve la cartella che lo contiene
 * (nomi lunghi su Windows, link simbolici su POSIX) e si aggiunge il nome.
 */
eGsErr_t sys_abs_path(const char *path, Str_t *out);

/* Bit di esecuzione per tutti (POSIX); su Windows non serve: 0 = riuscito. */
int sys_make_executable(const char *path);

/* Copia i permessi di `from` su `to` (POSIX); su Windows niente: 0 = riuscito. */
int sys_copy_mode(const char *from, const char *to);

#endif /* GS_SYS_H */
