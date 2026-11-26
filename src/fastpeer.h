/*
 * fastpeer.h - update --peers senza processi per i casi semplici (D-116).
 *
 * Il padre di update --peers legge lo stato dei peer con la cartella .git
 * da file e da un solo processo git, e decide per ogni file:
 *   - allineato: niente, 0 processi;
 *   - solo la ref: il file e' gia' quello di T; oggetti e refs/shared/<id>
 *     scritti dal padre, 0 processi;
 *   - da applicare: status, restore da un tree sintetico (i soli file da
 *     applicare, scritto dal padre), commit --only: 3 processi per peer,
 *     a fasi, tutti i peer insieme.
 * Tutto il resto, o una lettura che non torna, va al figlio di D-115
 * (ripiego): un peer con un file di ripiego va tutto al figlio, perche'
 * index e commit di una repo li deve fare uno solo.
 *
 * Gli oggetti di tutte le repo si leggono con un cat-file --batch che ha
 * le cartelle degli oggetti dei peer come alternate (GIT_ALTERNATE_OBJECT_
 * DIRECTORIES, solo per quel processo). Gli oggetti sono cercati per hash:
 * leggere un oggetto di un peer da li' e' come leggerlo nel peer.
 *
 * Il padre scrive nel peer (eccezione a D-099, solo per --peers): gli
 * oggetti delle versioni mancanti, sciolti (file temporaneo e rinomina), e
 * refs/shared/<id> con il lock di git e la rilettura sotto lock. Ogni stato
 * intermedio di un'interruzione e' uno stato normale di git shared.
 */
#ifndef GS_FASTPEER_H
#define GS_FASTPEER_H

#include <stddef.h>

#include "bounds.h"
#include "err.h"
#include "git.h"
#include "proc.h"
#include "str.h"

/* Esito di un file in un peer. */
typedef enum {
    FP_FALLBACK = 0, /* deciso dal figlio di D-115 */
    FP_ALIGNED,      /* niente da fare */
    FP_REF_ONLY,     /* oggetti e ref: il file e' gia' quello di T */
    FP_APPLY,        /* oggetti, ref, file, commit */
    FP_LOCAL,        /* modifiche locali: oggetti e ref, il file resta */
    FP_DONE,         /* applicato e committato */
    FP_FAILED        /* non riuscito: il messaggio e' nell'uscita del peer */
} eFpKind_t;

/* Esito di un peer. */
typedef enum {
    FPP_FAST = 0, /* tutti i suoi file nel percorso veloce */
    FPP_FALLBACK, /* al figlio di D-115 */
    FPP_OFF       /* shared.peerUpdate = false */
} eFpPeerKind_t;

/* Una versione della catena: commit, tree e voci del tree. */
typedef struct {
    Oid_t commit;
    Oid_t tree;
    Oid_t content;    /* blob del file */
    Oid_t creator;    /* voci dell'autorita' (len 0 se mancano) */
    Oid_t sovereigns;
    Oid_t heirs;
    size_t parent[2]; /* indici in FpChain_t.v */
    unsigned char np; /* genitori trovati */
    unsigned char mark;
} FpNode_t;

/* La catena di T di un file, letta una volta per tutti i peer. */
typedef struct {
    char id[GS_ID_HEX_LEN + 1U];
    Oid_t t;       /* T del padre (len 0: nessuna) */
    FpNode_t *v;   /* versioni raggiungibili da T; v[0] = T */
    size_t n;
    size_t cap;
    int ok;        /* letta per intero ed entro il limite */
} FpChain_t;

/* Un file in un peer. */
typedef struct {
    size_t chain;  /* indice della sua catena */
    Str_t path;    /* path nel peer, da .gitshared */
    Oid_t tp;      /* T del peer */
    Oid_t head;    /* blob del file in HEAD del peer */
    unsigned char kind; /* eFpKind_t */
} FpFile_t;

/* Un peer. */
typedef struct {
    Str_t root;    /* radice */
    Str_t git_dir; /* <root>/.git */
    Str_t ids;     /* ID dei suoi file, separati da virgole (per il figlio) */
    FpFile_t *f;
    size_t n;
    size_t cap;
    unsigned char kind; /* eFpPeerKind_t */
    int rc;        /* codice del peer: 0, o 1 se resta qualcosa da fare */
    Str_t out;     /* righe per l'utente, senza il path del peer */
    Str_t src;     /* "--source=<tree sintetico>" per git restore */
    Proc_t p;      /* processo della fase in corso, o il figlio */
    int started;
    int done;      /* figlio raccolto e stampato */
} FpPeer_t;

/* Lettore degli oggetti di tutte le repo. */
typedef struct {
    Proc_t p;
    Str_t line;
    int open;
} FpReader_t;

void fp_peer_init(FpPeer_t *pp, const char *root);
void fp_peer_free(FpPeer_t *pp);
/* Aggiunge un file (l'ID e' quello della catena) al peer. */
void fp_peer_add(FpPeer_t *pp, size_t chain, const char *id);
void fp_chain_free(FpChain_t *c);

/*
 * Avvia il cat-file con le cartelle degli oggetti dei peer come alternate.
 * Un peer senza cartella .git, o con un path che non si puo' mettere
 * nell'elenco, diventa FPP_FALLBACK. La variabile si toglie subito
 * dall'ambiente: gli altri processi non la vedono.
 */
eGsErr_t fp_reader_open(FpReader_t *rd, FpPeer_t *peers, size_t n);
void fp_reader_close(FpReader_t *rd);

/* Legge la catena di c->t (al piu' FP_CHAIN_MAX versioni). */
#define FP_CHAIN_MAX 4096U
void fp_chain_load(FpReader_t *rd, FpChain_t *c);

/* Classifica i file del peer; objfmt = formato degli oggetti del padre. */
void fp_classify(FpReader_t *rd, FpPeer_t *pp, FpChain_t *chains,
                 const char *objfmt);

/* Fase 1: git status nei peer con file da applicare, tutti insieme. */
void fp_status_start(FpPeer_t *peers, size_t n);
/* Raccoglie gli status: file con modifiche locali -> FP_LOCAL; una
 * modifica in .gitshared, o uno status fallito -> il peer va al figlio. */
void fp_status_finish(FpPeer_t *peers, size_t n);

/*
 * Il resto, per i peer FPP_FAST: oggetti, ref e tree sintetico scritti dal
 * padre, poi restore e commit, ogni fase tutti i peer insieme.
 * message = messaggio del commit (NULL: "Update <file>").
 */
void fp_apply(FpReader_t *rd, FpPeer_t *peers, size_t n,
              FpChain_t *chains, const char *message);

#endif /* GS_FASTPEER_H */
