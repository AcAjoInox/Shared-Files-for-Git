/*
 * shared.h - modello: i file shared dichiarati in .gitshared (§6.3).
 *
 * Nella v0 (M0) solo lettura di .gitshared. Seguiranno l'unione con le
 * sezioni locali di .git/config e la derivazione dello stato (D-054, D-065).
 */
#ifndef GS_SHARED_H
#define GS_SHARED_H

#include <stddef.h>

#include "err.h"
#include "str.h"

typedef struct {
    Str_t name; /* nome breve: nome della sezione in .gitshared */
    Str_t id;   /* 32 cifre esadecimali minuscole (D-006) */
    Str_t path; /* relativo alla radice della repo, separatore '/' */
} SharedEntry_t;

typedef struct {
    SharedEntry_t *v;
    size_t n;
    size_t cap;
} SharedList_t;

#define SHARED_LIST_INIT { NULL, 0U, 0U }

void shared_list_free(SharedList_t *l);

/*
 * Legge <top>/.gitshared, nell'ordine del file. Un file assente da' una
 * lista vuota. GS_ERR_FORMAT se il file non e' un config di git valido
 * (git ha gia' scritto su stderr la riga sbagliata).
 */
eGsErr_t shared_load(const char *top, SharedList_t *l);

/* 1 se la voce ha un ID ben formato e un path relativo, 0 altrimenti. */
int shared_entry_valid(const SharedEntry_t *e);

#endif /* GS_SHARED_H */
