/*
 * err.h - codici di errore interni e codici di uscita (§8.4).
 */
#ifndef GS_ERR_H
#define GS_ERR_H

/* Esito delle funzioni interne che possono fallire. */
typedef enum {
    GS_OK = 0,
    GS_ERR_USAGE,    /* argomenti errati */
    GS_ERR_NOT_REPO, /* fuori da una repo git o dal suo working tree */
    GS_ERR_PROC,     /* impossibile lanciare un processo */
    GS_ERR_GIT,      /* un comando git e' terminato con errore */
    GS_ERR_FORMAT,   /* dati in un formato inatteso */
    GS_ERR_SYS       /* errore del sistema operativo (file, numeri casuali) */
} eGsErr_t;

/* Codici di uscita del programma, come git (REQ-NF-008). */
enum {
    GS_EXIT_OK = 0,
    GS_EXIT_ERROR = 1,
    GS_EXIT_FATAL = 128,
    GS_EXIT_USAGE = 129
};

#endif /* GS_ERR_H */
