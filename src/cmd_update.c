/*
 * cmd_update.c - "git shared update [-f] [--scan] [-c | -m <msg>] [<repo>]
 * [<path>]" (REQ-F-009, REQ-F-010, REQ-F-011, D-040, D-078, D-099, D-108,
 * D-115).
 *
 * <repo> (D-115): se il primo argomento e' una cartella, update ci entra
 * prima di aprire la repo, come "git -C <repo> shared update [<path>]";
 * <path> e' allora relativo a quella cartella. Un file shared non e' mai
 * una cartella: nessuna ambiguita'.
 *
 * "git shared update --peers [--scan] [-m <msg>] [<path>]" (D-115): scrive
 * l'avviso della propria T in ogni peer dei file scelti, avvia in parallelo
 * in ogni peer "git-shared update --as-peer <id>,... -- <peer>", intanto
 * aggiorna questa repo, e committa ovunque. Il figlio (--as-peer, interna)
 * fa solo i casi sicuri: applica una versione in fast-forward su un file
 * senza modifiche locali; salta divergenze, unioni, file sospesi, repo con
 * merge/rebase in corso o HEAD staccata, e le repo con
 * shared.peerUpdate = false. Il padre stampa l'uscita di ogni figlio con il
 * path del peer davanti, nell'ordine dei peer.
 *
 * Per ogni file shared (o quello indicato):
 *   1. ricezione (receive.h): fetch solo dalle repo che hanno annunciato
 *      una versione che qui manca, da tutti i peer con --scan; in remoto
 *      l'ultima versione vista del creatore, senza rete (D-110); se una
 *      versione ricevuta discende da T, T avanza in fast-forward; le
 *      versioni che non discendono da T restano da unire;
 *   2. un'unione in corso con la risoluzione in staging viene conclusa;
 *   3. il branch va a T: "da applicare" -> T nel working tree e
 *      nell'index; "divergente" -> unione a 3 vie (base, H, T);
 *   4. le versioni dei peer rimaste si uniscono una alla volta.
 * Ogni unione pulita crea la versione nella catena prima del commit:
 * dopo, il file e' "da committare" (D-076). Al primo conflitto ci si
 * ferma: stage nell'index, marker nel file (per un binario la versione
 * nostra, senza marker: D-108), refs/shared-merge/<id>.
 * Non committa, salvo -c/-m (D-007, D-075). Modifiche locali: rifiuta,
 * salvo -f; una versione applicata e non committata non lo e' (D-076).
 *
 * "git shared update --abort [<path>]" (D-093): rinuncia all'unione in
 * corso, senza fetch; il file torna quello di HEAD.
 *
 * "git shared update --ours | --theirs [<path>]" (D-108): conclude l'unione
 * in corso con la versione nostra (stage 2) o con quella ricevuta (stage
 * 3), intera, per binari e testo; poi prosegue come update.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chain.h"
#include "cmd.h"
#include "cmd_common.h"
#include "fastpeer.h"
#include "inbox.h"
#include "mem.h"
#include "merge.h"
#include "peers.h"
#include "proc.h"
#include "receive.h"
#include "state.h"
#include "sys.h"

/* Esito di un passo di unione. */
typedef enum {
    STEP_DONE = 0, /* risultato nell'index e nel working tree */
    STEP_CONFLICT, /* conflitto lasciato all'utente */
    STEP_ERROR
} eStep_t;

/* Elenchi dei file toccati, per il commit e i messaggi finali. */
typedef struct {
    Str_t applied; /* da committare (o committati con -c) */
    Str_t merged;  /* tra questi, quelli nati da un'unione */
} Touched_t;

static void add_once(Str_t *list, const char *label)
{
    if (!str_zhas(list, label)) {
        str_addz(list, label);
    }
}

/* Un cat-file nuovo, che veda l'index appena modificato. */
static eGsErr_t refresh(GitBatch_t *b, int *batch_ok)
{
    eGsErr_t err;

    git_batch_close(b);
    err = git_batch_open(b);
    if (err != GS_OK) {
        msg_fatal(MSG_ERR_PROC, "git cat-file");
        *batch_ok = 0;
    }
    return err;
}

/* Blob di una versione: "<commit>:content". */
static int version_blob(GitBatch_t *b, const Oid_t *commit, Oid_t *blob)
{
    char hex[OID_HEX_SIZE];
    Str_t name = STR_INIT;
    int ok;

    oid_to_hex(commit, hex);
    str_addf(&name, "%s:content", hex);
    ok = git_batch_blob(b, str_cstr(&name), blob);
    str_free(&name);
    return ok;
}

/* Toglie refs/shared-merge/<id>, se c'e'. */
static void drop_marker(const SharedEntry_t *e, GitBatch_t *b)
{
    Str_t ref = STR_INIT;
    Oid_t k;

    merge_ref(&ref, e->id);
    if (git_batch_query(b, str_cstr(&ref), &k) == GIT_OBJ_COMMIT) {
        (void)merge_drop(e->id);
    }
    str_free(&ref);
}

/*
 * Un passo di unione (D-078): contenuto `ours` (quello del branch o
 * dell'index) con la versione di blob `theirs`, partendo da `base` (NULL =
 * nessuna base: base vuota, D-029).
 *
 * Se riesce, il risultato e' nell'index e nel working tree (*result) e,
 * se porta qualcosa di nuovo, diventa una versione con genitori *tip e p2
 * (NULL = uno solo): la ref avanza solo se vale ancora *tip, che viene
 * aggiornato. Un risultato uguale a `tip_blob` senza secondo genitore e'
 * la versione che c'e' gia': nessuna versione nuova.
 *
 * In conflitto: refs/shared-merge/<id> registra *tip e p2, le versioni
 * che la risoluzione incorporera'.
 */
static eStep_t merge_step(const Repo_t *r, const SharedEntry_t *e,
                          GitBatch_t *b, const Oid_t *base, const Oid_t *ours,
                          const Oid_t *theirs, const Oid_t *tip_blob,
                          const Oid_t *p2, Oid_t *tip, Oid_t *result)
{
    const char *top = str_cstr(&r->top);
    const char *path = str_cstr(&e->path);
    Str_t ref = STR_INIT;
    AuthBlobs_t auth;
    Oid_t empty;
    Oid_t v;
    int conflict = 0;
    int auth_conflict = 0;
    eGsErr_t err = GS_OK;
    eStep_t step = STEP_ERROR;

    /* 1. Il contenuto. I casi senza unione vera non passano dagli stage. */
    if (((base != NULL) && oid_eq(theirs, base)) || oid_eq(ours, theirs)) {
        *result = *ours; /* niente di nuovo dall'altra parte */
        err = cmd_write_file(r, path, result);
    } else if ((base != NULL) && oid_eq(ours, base)) {
        *result = *theirs; /* niente di nostro: e' un'applicazione */
        err = cmd_write_file(r, path, result);
    } else {
        if (base == NULL) {
            err = merge_empty_blob(&empty);
            base = &empty;
        }
        if (err == GS_OK) {
            err = merge_three(top, path, base, ours, theirs, &conflict);
        }
        if ((err == GS_OK) && (conflict == 0)) {
            err = merge_index_blob(top, path, result);
        }
    }
    if (err != GS_OK) {
        msg_error(MSG_ERR_MERGE, path);
        goto exit;
    }

    /* 2. Conflitto: l'unione resta registrata. Il commit provvisorio ha
     * per genitori le versioni che si stanno unendo. */
    if (conflict != MERGE_CLEAN) {
        merge_ref(&ref, e->id);
        if ((chain_commit(theirs, tip, p2, NULL, str_cstr(&e->name), &v) != GS_OK) ||
            (git_set_ref(str_cstr(&ref), &v, "git shared: merge") != GS_OK)) {
            msg_error(MSG_ERR_MERGE, path);
            goto exit;
        }
        if (conflict == MERGE_BINARY) {
            msg_error(MSG_ERR_CONFLICT_BINARY, path, path, path);
        } else {
            msg_error(MSG_ERR_CONFLICT_OPEN, path, path);
        }
        step = STEP_CONFLICT;
        goto exit;
    }

    /* 3. Unione pulita: la versione nasce prima del commit, cosi' dopo lo
     * stato e' "da committare" senza altri dati salvati. */
    if ((p2 != NULL) || !oid_eq(result, tip_blob)) {
        /* Creatore e sovrani: unione a 3 vie della voce autorita' (D-111).
         * Cambiata in modo diverso da entrambi i lati: vince la versione
         * del peer solo se la sua autorita' e' stata accettata, come
         * l'utente ha scelto (REQ-F-030); altrimenti resta quella di T. */
        if (auth_merge(b, tip, p2, &auth, &auth_conflict) != GS_OK) {
            msg_error(MSG_ERR_MERGE, path);
            goto exit;
        }
        if ((auth_conflict != 0) && receive_auth_accepted(e, b, p2)) {
            auth_blobs(b, p2, &auth);
        }
        chain_ref(&ref, e->id);
        if ((chain_commit(result, tip, p2, &auth, str_cstr(&e->name), &v) != GS_OK) ||
            (git_update_ref(str_cstr(&ref), &v, tip, "git shared: merge") != GS_OK)) {
            /* es. un peer ha inviato una versione nel frattempo: il
             * risultato resta nell'index, il prossimo update riparte */
            msg_error(MSG_ERR_CHAIN, str_cstr(&e->name));
            goto exit;
        }
        *tip = v;
    }
    drop_marker(e, b);
    step = STEP_DONE;

exit:
    str_free(&ref);
    return step;
}

/*
 * Unione registrata in refs/shared-merge/<id>. Ritorna 1 se si puo'
 * proseguire con lo stato (nessuna unione, o conclusa), 0 se il file e'
 * da saltare (*rc aggiornato). Con --ours / --theirs (choose_entry) la
 * versione scelta e' gia' nell'index: e' una risoluzione anche se uguale
 * a HEAD, quindi non si guarda se l'unione e' scaduta (D-108).
 */
static int settle_merge(const Repo_t *r, const SharedEntry_t *e, GitBatch_t *b,
                        const CmdOpts_t *o, Touched_t *touched, int *rc,
                        int *batch_ok)
{
    const int chosen = (o->ours != 0U) || (o->theirs != 0U);
    const char *label = str_cstr(&e->path);
    char hex[OID_HEX_SIZE];
    Str_t name = STR_INIT;
    Oid_t k;      /* commit provvisorio dell'unione */
    Oid_t old;    /* T all'inizio dell'unione */
    Oid_t p2;     /* versione del peer, se l'unione era tra versioni */
    Oid_t h;
    Oid_t i;
    Oid_t base;
    Oid_t tip;
    Oid_t t;
    Oid_t result;
    Oid_t before; /* T prima della conclusione */
    int unmerged = 0;
    int has_p2;
    int go_on = 0;
    eStep_t step;

    merge_ref(&name, e->id);
    if (git_batch_query(b, str_cstr(&name), &k) != GIT_OBJ_COMMIT) {
        go_on = 1; /* nessuna unione in corso */
        goto exit;
    }
    str_reset(&name);
    str_addf(&name, "HEAD:%s", label);
    if ((chosen == 0) &&
        ((git_batch_blob(b, str_cstr(&name), &h) == 0) ||
         !merge_is_live(b, label, &h, &unmerged))) {
        /* Abbandonata (git restore): l'index e' tornato a HEAD. */
        drop_marker(e, b);
        msg_warning(MSG_WARN_MERGE_DROPPED, label);
        go_on = 1;
        goto exit;
    }
    if (unmerged != 0) {
        msg_error(MSG_ERR_CONFLICT_OPEN, label, label);
        *rc = GS_EXIT_ERROR;
        goto exit;
    }
    /* Risoluzione in staging: e' lei il contenuto da unire. */
    if ((o->force == 0U) && cmd_worktree_differs(r, label)) {
        msg_error(MSG_ERR_LOCAL_CHANGES, label);
        *rc = GS_EXIT_ERROR;
        goto exit;
    }
    oid_to_hex(&k, hex);
    str_reset(&name);
    str_addf(&name, "%s^1", hex);
    if (git_batch_query(b, str_cstr(&name), &old) != GIT_OBJ_COMMIT) {
        msg_error(MSG_ERR_MERGE, label);
        *rc = GS_EXIT_ERROR;
        goto exit;
    }
    str_reset(&name);
    str_addf(&name, "%s^2", hex);
    has_p2 = (git_batch_query(b, str_cstr(&name), &p2) == GIT_OBJ_COMMIT);
    chain_ref(&name, e->id);
    if ((git_batch_query(b, str_cstr(&name), &tip) != GIT_OBJ_COMMIT) ||
        !version_blob(b, &tip, &t) || !version_blob(b, &old, &base) ||
        (merge_index_blob(str_cstr(&r->top), label, &i) != GS_OK)) {
        msg_error(MSG_ERR_MERGE, label);
        *rc = GS_EXIT_ERROR;
        goto exit;
    }
    /* La risoluzione incorpora `old` (e p2). Se T e' ancora `old` diventa
     * la nuova versione cosi' com'e'; se nel frattempo e' avanzata
     * (reportIdea B-17) e' un'altra unione, con base `old`. */
    before = tip;
    step = merge_step(r, e, b, &base, &i, &t, &t, has_p2 ? &p2 : NULL, &tip,
                      &result);
    if (refresh(b, batch_ok) != GS_OK) {
        *rc = GS_EXIT_FATAL;
        goto exit;
    }
    if (step != STEP_DONE) {
        *rc = GS_EXIT_ERROR;
        goto exit;
    }
    /* Nessuna versione nuova: la risoluzione e' T (es. --theirs su una
     * divergenza del branch). E' un'applicazione, non un'unione. */
    if (!oid_eq(&tip, &before)) {
        add_once(&touched->merged, label);
    }
    go_on = 1;

exit:
    str_free(&name);
    return go_on;
}

/* 1 se il file ha un'unione in corso, refs/shared-merge/<id> (D-078). */
static int merge_in_progress(const SharedEntry_t *e, GitBatch_t *b)
{
    Str_t name = STR_INIT;
    Oid_t k;
    int live;

    merge_ref(&name, e->id);
    live = (git_batch_query(b, str_cstr(&name), &k) == GIT_OBJ_COMMIT);
    str_free(&name);
    return live;
}

/* Applica T al file (D-042); rifiuta se ci sono modifiche locali. */
static int apply(const Repo_t *r, const SharedEntry_t *e, GitBatch_t *b,
                 const StateInfo_t *si, int force)
{
    const char *label = str_cstr(&e->path);
    int ok = 0;

    if (!force && cmd_has_local_changes(r, e, b)) {
        msg_error(MSG_ERR_LOCAL_CHANGES, label); /* REQ-F-010, D-022 */
    } else if (cmd_write_file(r, label, &si->t) != GS_OK) {
        msg_error(MSG_ERR_WRITE_FILE, label);
    } else {
        ok = 1;
    }
    return ok;
}

/* Un file: ricezione, unione in corso, stato, unioni. Ritorna 0 solo se
 * il comando non puo' proseguire (*rc = GS_EXIT_FATAL). */
static int update_entry(const Repo_t *r, const SharedEntry_t *e, GitBatch_t *b,
                        const CmdOpts_t *o, Touched_t *touched, int *rc,
                        int *batch_ok, BranchNames_t *bn)
{
    const char *label = str_cstr(&e->path);
    Received_t rv = RECEIVED_INIT; /* versioni dei peer da unire: rv.heads */
    StateInfo_t si;
    eState_t st;
    Oid_t cur;    /* contenuto attuale dell'index, se e' noto */
    Oid_t tip;
    Oid_t mb;
    Oid_t base;
    Oid_t theirs;
    Oid_t result;
    size_t k;
    int was_merged;
    int go_on = 1;
    int merged_peer = 0; /* il ciclo sotto ha unito una versione */
    /* Messaggio rimandato: se restano versioni dei peer, lo stato lo dice
     * l'unione; se sono tutte gia' contenute in T, si stampa questo. */
    eMsgId_t later = MSG_COUNT;
    eStep_t step;

    cur.len = 0U;
    if (receive_entry(r, e, b, (int)o->scan, &rv) != GS_OK) {
        msg_error(MSG_ERR_CHAIN, str_cstr(&e->name));
        *rc = GS_EXIT_ERROR;
        goto exit;
    }
    if ((rv.advanced != 0) && (o->as_peer == 0U)) {
        /* nel peer lo dice gia' la riga che segue (D-115) */
        printf(msg(MSG_UPD_RECEIVED), label);
        putchar('\n');
    }
    /* Un cambio di autorita' ricevuto si decide prima (D-111): T si e'
     * fermata all'ultima versione con l'autorita' di prima. */
    cmd_print_auth_changes(e, &rv, 1);
    /* Nel peer (D-115) un'unione in corso la conclude solo chi la sta
     * facendo: nemmeno una risoluzione gia' in staging. */
    if ((o->as_peer != 0U) && merge_in_progress(e, b)) {
        printf(msg(MSG_UPD_PEER_MERGING), label);
        putchar('\n');
        *rc = GS_EXIT_ERROR;
        goto exit;
    }
    if (!settle_merge(r, e, b, o, touched, rc, batch_ok)) {
        go_on = (*batch_ok != 0);
        goto exit;
    }
    was_merged = str_zhas(&touched->merged, label);
    if (state_derive(r, e, b, &st, &si) != GS_OK) {
        msg_fatal(MSG_ERR_PROC, "git");
        *rc = GS_EXIT_FATAL;
        go_on = 0;
        goto exit;
    }
    if (state_is_derived(st) && !shared_is_active(e, str_cstr(&r->branch))) {
        if (o->as_peer != 0U) {
            /* lanciato da un'altra repo: non e' un atto su questo branch */
            printf(msg(MSG_UPD_PEER_SUSPENDED), label);
            putchar('\n');
            goto exit;
        }
        cmd_warn_suspended(e, bn); /* atto esplicito: si fa */
    }
    tip = si.tip;

    /* --- il branch va a T ------------------------------------------------ */
    if (st == STATE_ALIGNED) {
        cur = si.t;
        if (was_merged) {
            printf(msg(MSG_UPD_MERGED_PUBLISH), label);
            putchar('\n');
        } else {
            /* con versioni dei peer: si unisce sotto */
            later = MSG_UPD_UP_TO_DATE;
        }
    } else if (st == STATE_TO_APPLY) {
        if ((o->as_peer != 0U) && cmd_has_local_changes(r, e, b)) {
            printf(msg(MSG_UPD_PEER_LOCAL), label);
            putchar('\n');
            *rc = GS_EXIT_ERROR;
        } else if (apply(r, e, b, &si, (int)(o->force | o->as_peer))) {
            add_once(&touched->applied, label);
            cur = si.t;
        } else {
            *rc = GS_EXIT_ERROR;
        }
    } else if (st == STATE_TO_COMMIT) {
        /* Gia' applicata o unita (D-076): con -c si committa, se il
         * working tree e' ancora quello (--only committa il WT). */
        cur = si.t;
        if (o->commit == 0U) {
            later = was_merged ? MSG_UPD_MERGED : MSG_UPD_TO_COMMIT;
        } else if (cmd_worktree_differs(r, label)) {
            if (o->as_peer != 0U) {
                printf(msg(MSG_UPD_PEER_LOCAL), label);
                putchar('\n');
            } else {
                msg_error(MSG_ERR_LOCAL_CHANGES, label);
            }
            *rc = GS_EXIT_ERROR;
            cur.len = 0U;
        } else {
            add_once(&touched->applied, label);
        }
    } else if (st == STATE_TO_PUBLISH) {
        cur = si.h;
        later = MSG_UPD_TO_PUBLISH;
        if (o->peers != 0U) {
            msg_warning(MSG_WARN_PEERS_UNPUBLISHED, label);
        }
    } else if ((st == STATE_DIVERGED) && (o->as_peer != 0U)) {
        /* un'unione la fa chi guarda la repo (D-115) */
        printf(msg(MSG_UPD_PEER_DIVERGED), label);
        putchar('\n');
        *rc = GS_EXIT_ERROR;
    } else if (st == STATE_DIVERGED) {
        /* Modifica committata da una base vecchia: base, H, T. */
        if ((o->force == 0U) && cmd_has_local_changes(r, e, b)) {
            msg_error(MSG_ERR_LOCAL_CHANGES, label);
            *rc = GS_EXIT_ERROR;
        } else {
            step = merge_step(r, e, b, (si.base.len > 0U) ? &si.base : NULL,
                              &si.h, &si.t, &si.t, NULL, &tip, &result);
            if (refresh(b, batch_ok) != GS_OK) {
                *rc = GS_EXIT_FATAL;
                go_on = 0;
                goto exit;
            }
            if (step != STEP_DONE) {
                *rc = GS_EXIT_ERROR;
            } else {
                cur = result;
                add_once(&touched->merged, label);
                if (oid_eq(&result, &si.h)) {
                    /* H conteneva gia' T (es. dopo uno squash) */
                    printf(msg(MSG_UPD_MERGED_PUBLISH), label);
                    putchar('\n');
                } else {
                    add_once(&touched->applied, label);
                }
            }
        }
    } else if (st == STATE_MERGING) {
        msg_error(MSG_ERR_CONFLICT_OPEN, label, label);
        *rc = GS_EXIT_ERROR;
    } else {
        printf(msg(MSG_SKIP_STATE), label, state_name(st));
        putchar('\n');
    }

    /* --- versioni dei peer che non discendono da T: una alla volta ------- */
    for (k = 0U; (k < rv.heads.n) && (cur.len > 0U); k++) {
        if (merge_base(&tip, &rv.heads.v[k], &mb) != GS_OK) {
            msg_error(MSG_ERR_MERGE, label);
            *rc = GS_EXIT_ERROR;
            break;
        }
        /* Gia' contenuta in T: la conclusione di un'unione in corso
         * (settle_merge) l'ha appena unita. Unirla di nuovo creerebbe una
         * seconda versione con lo stesso contenuto (reportIdea B-49). */
        if ((mb.len > 0U) && oid_eq(&mb, &rv.heads.v[k])) {
            continue;
        }
        merged_peer = 1;
        if (o->as_peer != 0U) {
            /* nessuna unione nel peer (D-115): resta a chi ci lavora */
            printf(msg(MSG_UPD_PEER_TO_MERGE), label);
            putchar('\n');
            *rc = GS_EXIT_ERROR;
            break;
        }
        if ((o->force == 0U) && cmd_has_local_changes(r, e, b)) {
            msg_error(MSG_ERR_LOCAL_CHANGES, label);
            *rc = GS_EXIT_ERROR;
            break;
        }
        if (!version_blob(b, &rv.heads.v[k], &theirs) ||
            ((mb.len > 0U) && !version_blob(b, &mb, &base))) {
            msg_error(MSG_ERR_MERGE, label);
            *rc = GS_EXIT_ERROR;
            break;
        }
        step = merge_step(r, e, b, (mb.len > 0U) ? &base : NULL, &cur, &theirs,
                          &cur, &rv.heads.v[k], &tip, &result);
        if (refresh(b, batch_ok) != GS_OK) {
            *rc = GS_EXIT_FATAL;
            go_on = 0;
            goto exit;
        }
        if (step != STEP_DONE) {
            *rc = GS_EXIT_ERROR;
            break;
        }
        cur = result;
        add_once(&touched->merged, label);
        if (oid_eq(&result, &si.h)) {
            printf(msg(MSG_UPD_MERGED_PUBLISH), label);
            putchar('\n');
        } else {
            add_once(&touched->applied, label);
        }
    }
    if ((later != MSG_COUNT) && (merged_peer == 0) && (cur.len > 0U)) {
        printf(msg(later), label);
        putchar('\n');
    }

exit:
    received_free(&rv);
    return go_on;
}

/*
 * --ours / --theirs (D-108): la versione scelta, presa dallo stage 2
 * (nostra) o 3 (loro), va nell'index al posto degli stage e nel working
 * tree; la conclusione la fa poi update_entry. Ritorna 1 se il file e'
 * pronto. named = file indicato dall'utente: senza unione e' un errore;
 * altrimenti si salta. Se gli stage non ci sono piu' la risoluzione e'
 * gia' in staging: la si conclude con update, o si rinuncia.
 */
static int choose_entry(const Repo_t *r, const SharedEntry_t *e, GitBatch_t *b,
                        const CmdOpts_t *o, int named, int *rc, int *found)
{
    const char *label = str_cstr(&e->path);
    Str_t name = STR_INIT;
    Oid_t k;
    Oid_t v;
    int ok = 0;

    merge_ref(&name, e->id);
    if (git_batch_query(b, str_cstr(&name), &k) != GIT_OBJ_COMMIT) {
        if (named != 0) {
            msg_error(MSG_ERR_NO_MERGE, label);
            *rc = GS_EXIT_ERROR;
        }
        goto exit;
    }
    *found = 1;
    str_reset(&name);
    str_addf(&name, ":%c:%s", (o->ours != 0U) ? '2' : '3', label);
    if (git_batch_blob(b, str_cstr(&name), &v) == 0) {
        msg_error(MSG_ERR_CHOOSE_STAGED, label, label);
        *rc = GS_EXIT_ERROR;
        goto exit;
    }
    /* update-index --cacheinfo allo stage 0 toglie gli stage 1/2/3 */
    if (cmd_write_file(r, label, &v) != GS_OK) {
        msg_error(MSG_ERR_WRITE_FILE, label);
        *rc = GS_EXIT_ERROR;
        goto exit;
    }
    ok = 1;

exit:
    str_free(&name);
    return ok;
}

/*
 * --abort (D-093): rinuncia all'unione in corso del file. Il file torna
 * quello di HEAD nell'index (gli stage di conflitto spariscono) e nel
 * working tree, poi si toglie refs/shared-merge/<id>. T non cambia. In un
 * solo comando: nessun momento in cui una modifica messa in staging possa
 * passare per la risoluzione (reportIdea B-48). named = file indicato
 * dall'utente: senza unione e' un errore; altrimenti si salta.
 */
static void abort_entry(const Repo_t *r, const SharedEntry_t *e, GitBatch_t *b,
                        int named, int *rc, int *found)
{
    const char *label = str_cstr(&e->path);
    Str_t name = STR_INIT;
    Oid_t k;
    Oid_t h;

    merge_ref(&name, e->id);
    if (git_batch_query(b, str_cstr(&name), &k) != GIT_OBJ_COMMIT) {
        if (named != 0) {
            msg_error(MSG_ERR_NO_MERGE, label);
            *rc = GS_EXIT_ERROR;
        }
        goto exit;
    }
    *found = 1;
    /* Prima il file, poi la ref: se il file non si scrive l'unione resta
     * registrata e il comando si puo' ripetere. */
    str_reset(&name);
    str_addf(&name, "HEAD:%s", label);
    if ((git_batch_blob(b, str_cstr(&name), &h) == 0) ||
        (cmd_write_file(r, label, &h) != GS_OK)) {
        msg_error(MSG_ERR_WRITE_FILE, label);
        *rc = GS_EXIT_ERROR;
        goto exit;
    }
    if (merge_drop(e->id) != GS_OK) {
        /* il file e' gia' quello di HEAD: la ref e' scaduta e la toglie
         * il prossimo comando (D-092) */
        msg_error(MSG_ERR_MERGE, label);
        *rc = GS_EXIT_ERROR;
        goto exit;
    }
    printf(msg(MSG_UPD_ABORTED), label);
    putchar('\n');

exit:
    str_free(&name);
}

int cmd_update_run(Repo_t *r, GitBatch_t *b, const unsigned char *sel,
                   CmdOpts_t *o, int rc, int *batch_ok)
{
    Touched_t touched = { STR_INIT, STR_INIT };
    BranchNames_t names = BRANCHNAMES_INIT;
    Str_t text = STR_INIT;
    const char *p;
    eMsgId_t id;
    size_t i;
    int committed = 0;

    for (i = 0U; i < r->n; i++) {
        if ((sel[i] != 0U) &&
            !update_entry(r, &r->v[i], b, o, &touched, &rc, batch_ok, &names)) {
            goto exit;
        }
    }

    /* Commit dedicato dei soli file applicati o uniti (D-007). */
    if ((o->commit != 0U) && (touched.applied.len > 0U)) {
        if (o->message == NULL) {
            str_addf(&text, msg(MSG_UPD_COMMIT_MSG),
                     (str_zcount(&touched.applied) == 1U) ? touched.applied.buf
                                                         : "shared files");
            o->message = str_cstr(&text);
        }
        if (cmd_commit(r, o->message, &touched.applied) == GS_OK) {
            committed = 1;
        } else {
            msg_error(MSG_ERR_COMMIT);
            rc = GS_EXIT_ERROR;
        }
    }
    for (p = str_znext(&touched.applied, NULL); p != NULL;
         p = str_znext(&touched.applied, p)) {
        if (str_zhas(&touched.merged, p)) {
            id = (committed != 0) ? MSG_UPD_MERGED_COMMITTED : MSG_UPD_MERGED;
        } else {
            id = (committed != 0) ? MSG_UPD_COMMITTED : MSG_UPD_APPLIED;
        }
        printf(msg(id), p);
        putchar('\n');
    }

exit:
    str_free(&text);
    str_free(&touched.merged);
    str_free(&touched.applied);
    branchnames_free(&names);
    return rc;
}

/* --- update --peers (D-115) ------------------------------------------- */

/* 1 se nella cartella .git c'e' un'operazione di git a meta' (merge,
 * rebase, cherry-pick, revert, am): un commit ci finirebbe dentro. */
static int repo_busy(const Repo_t *r)
{
    static const char *const marks[] = { "MERGE_HEAD", "CHERRY_PICK_HEAD",
                                         "REVERT_HEAD", "rebase-merge",
                                         "rebase-apply" };
    Str_t path = STR_INIT;
    FILE *f;
    size_t i;
    int busy = 0;

    for (i = 0U; (i < (sizeof marks / sizeof marks[0])) && (busy == 0) &&
                 (r->git_dir.len > 0U);
         i++) {
        str_reset(&path);
        str_addf(&path, "%s/%s", str_cstr(&r->git_dir), marks[i]);
        if (sys_is_dir(str_cstr(&path))) {
            busy = 1;
        } else {
            f = sys_fopen(str_cstr(&path), "rb");
            if (f != NULL) {
                busy = 1;
                (void)fclose(f);
            }
        }
    }
    str_free(&path);
    return busy;
}

/* 1 se id e' nell'elenco "<id>,<id>,..." di --as-peer. */
static int id_listed(const char *ids, const char *id)
{
    const size_t n = strlen(id);
    const char *p = ids;
    int found = 0;

    while ((p != NULL) && (*p != '\0') && (found == 0)) {
        found = (strncmp(p, id, n) == 0) && ((p[n] == ',') || (p[n] == '\0'));
        p = strchr(p, ',');
        if (p != NULL) {
            p++;
        }
    }
    return found;
}

/*
 * Il figlio (--as-peer): sceglie le voci degli ID chiesti dal padre. Una
 * voce che c'e' ma non si puo' aggiornare (in attesa, non collegata, non
 * dichiarata su questo branch) lo dice in una riga. Ritorna quante voci
 * corrispondono a un ID, scelte o no.
 */
static size_t peer_select(const Repo_t *r, const char *ids, unsigned char *sel)
{
    const SharedEntry_t *e;
    size_t i;
    size_t found = 0U;

    for (i = 0U; i < r->n; i++) {
        e = &r->v[i];
        sel[i] = 0U;
        /* senza la sezione in .gitshared (file non dichiarato su questo
         * branch) l'ID viene da .git/config e valid resta 0 */
        if ((e->id[0] != '\0') && id_listed(ids, e->id)) {
            found++;
            if (e->valid && e->in_gitshared && e->in_config &&
                (e->pending == SHARED_PENDING_NONE)) {
                sel[i] = 1U;
            } else if (!e->in_gitshared) {
                printf(msg(MSG_UPD_PEER_SUSPENDED),
                       (e->path.len > 0U) ? str_cstr(&e->path) : e->id);
                putchar('\n');
            } else {
                printf(msg(MSG_SKIP_STATE), str_cstr(&e->path),
                       state_name((e->pending != SHARED_PENDING_NONE)
                                      ? STATE_PENDING
                                      : STATE_UNLINKED));
                putchar('\n');
            }
        }
    }
    return found;
}

/* Il figlio: controlli della repo, poi update con commit dei soli file
 * chiesti. Codice d'uscita 0 se non resta niente da fare a mano. */
static int peer_run(Repo_t *r, GitBatch_t *b, unsigned char *sel,
                    CmdOpts_t *o, int *batch_ok)
{
    eMsgId_t skip = MSG_COUNT;
    int rc = GS_EXIT_ERROR;

    if (r->no_peer_update != 0U) {
        skip = MSG_UPD_PEER_OFF; /* scelta di chi ci lavora: non e' un errore */
        rc = GS_EXIT_OK;
    } else if (r->branch.len == 0U) {
        skip = MSG_UPD_PEER_DETACHED;
    } else if (repo_busy(r)) {
        skip = MSG_UPD_PEER_BUSY;
    } else if (peer_select(r, o->peer_ids, sel) == 0U) {
        skip = MSG_UPD_PEER_NOTHING;
        rc = GS_EXIT_OK;
    } else {
        o->commit = 1U;
        rc = cmd_update_run(r, b, sel, o, GS_EXIT_OK, batch_ok);
    }
    if (skip != MSG_COUNT) {
        puts(msg(skip));
    }
    return rc;
}

/*
 * Path del peer da mostrare: relativo alla cartella corrente se hanno la
 * stessa radice ("../B"), altrimenti assoluto. Entrambi assoluti, con '/'.
 */
static void peer_display(const Repo_t *r, const char *peer, Str_t *out)
{
    Str_t cwd = STR_INIT;
    const char *c;
    const char *p = peer;
    const char *last_c; /* fine dell'ultimo elemento in comune */
    const char *last_p;

    str_reset(out);
    str_addf(&cwd, "%s/%s", str_cstr(&r->top), str_cstr(&r->prefix));
    if ((cwd.len > 0U) && (cwd.buf[cwd.len - 1U] == '/')) {
        str_setlen(&cwd, cwd.len - 1U);
    }
    c = str_cstr(&cwd);
    last_c = c;
    last_p = p;
    while ((*c != '\0') && (*c == *p)) {
        c++;
        p++;
        if (((*c == '/') || (*c == '\0')) && ((*p == '/') || (*p == '\0'))) {
            last_c = c;
            last_p = p;
        }
    }
    if (last_c == str_cstr(&cwd)) {
        str_add(out, peer); /* nessuna radice in comune (altro disco) */
    } else {
        for (c = last_c; *c != '\0'; c++) {
            if (*c == '/') {
                str_add(out, (out->len > 0U) ? "/.." : "..");
            }
        }
        if (*last_p == '/') {
            last_p++;
        }
        if ((out->len > 0U) && (*last_p != '\0')) {
            str_addch(out, '/');
        }
        str_add(out, last_p);
        if (out->len == 0U) {
            str_addch(out, '.');
        }
    }
    str_free(&cwd);
}

/* Stampa l'uscita di un figlio, ogni riga con il path del peer davanti. */
static void print_prefixed(const char *peer, const Str_t *out)
{
    const char *line = str_cstr(out);
    const char *end = line + out->len;
    const char *nl;
    size_t len;
    size_t step;

    while (line < end) {
        nl = memchr(line, '\n', (size_t)(end - line));
        len = (nl != NULL) ? (size_t)(nl - line) : (size_t)(end - line);
        step = len + ((nl != NULL) ? 1U : 0U);
        if ((len > 0U) && (line[len - 1U] == '\r')) {
            len--;
        }
        printf("%s: %.*s\n", peer, (int)len, line);
        line += step;
    }
}

/* Avvia il figlio di D-115 nel peer: lo stesso eseguibile, --as-peer. */
static void start_child(FpPeer_t *pp, const Str_t *exe, const CmdOpts_t *o)
{
    const char *args[9];
    size_t a = 0U;

    args[a++] = str_cstr(exe);
    args[a++] = "update";
    args[a++] = "--as-peer";
    args[a++] = str_cstr(&pp->ids);
    if (o->message != NULL) {
        args[a++] = "-m";
        args[a++] = o->message;
    }
    args[a++] = "--";
    args[a++] = str_cstr(&pp->root);
    args[a] = NULL;
    pp->started = (proc_start(&pp->p, args, PROC_STDERR_OUT | PROC_CLEAN_ENV) == GS_OK);
    if (!pp->started) {
        str_addf(&pp->out, "%s\n", msg(MSG_UPD_PEER_FAILED));
        pp->rc = 1;
    }
}

/* 1 se nella inbox di questa repo c'e' un avviso per un file scelto: una
 * versione che qui manca, da ricevere prima di propagare T. */
static int own_notices(const Repo_t *r, const unsigned char *sel)
{
    Inbox_t in = INBOX_INIT;
    size_t i;
    int any = 0;

    for (i = 0U; (i < r->n) && !any; i++) {
        if (sel[i] != 0U) {
            inbox_read(r, r->v[i].id, &in);
            any = (in.oids.n > 0U);
            inbox_free(&in);
            in = (Inbox_t)INBOX_INIT;
        }
    }
    return any;
}

/* Le righe di un peer, ognuna con il suo path davanti; poi il codice. */
static void print_peer(const Repo_t *r, FpPeer_t *pp, const Str_t *out, int *rc)
{
    Str_t shown = STR_INIT;

    peer_display(r, str_cstr(&pp->root), &shown);
    print_prefixed(str_cstr(&shown), out);
    (void)fflush(stdout);
    if ((pp->rc != 0) && (*rc == GS_EXIT_OK)) {
        *rc = GS_EXIT_ERROR;
    }
    pp->done = 1;
    str_free(&shown);
}

/* Raccoglie un figlio di ripiego terminato (o, con wait, anche no). */
static void collect_child(const Repo_t *r, FpPeer_t *pp, int *rc)
{
    Str_t out = STR_INIT;
    int code = -1;

    if (proc_collect(&pp->p, &out, &code) != GS_OK) {
        code = -1;
    }
    pp->started = 0;
    if (code < 0) {
        str_addf(&out, "%s\n", msg(MSG_UPD_PEER_FAILED));
    }
    if (code != 0) {
        pp->rc = 1;
    }
    print_peer(r, pp, &out, rc);
    str_free(&out);
}

/*
 * Il padre (D-115, D-116): per ogni peer che esiste, i file scelti che
 * condivide; il percorso veloce per i casi semplici (fastpeer.h), il figlio
 * di D-115 per gli altri. Con avvisi nella propria inbox questa repo si
 * aggiorna prima (la T da propagare cambia); altrimenti mentre i peer
 * lavorano. Righe dei peer veloci nell'ordine dei peer, poi i figli appena
 * finiscono.
 */
static int update_peers(Repo_t *r, GitBatch_t *b, unsigned char *sel,
                        CmdOpts_t *o, int rc, int *batch_ok)
{
    PeerTally_t tally = { 0U, 0U, 0U };
    FpReader_t rd;
    FpChain_t *chains;
    FpPeer_t *peers;
    const SharedEntry_t *e;
    const char *p;
    size_t *chain_of;
    Str_t exe = STR_INIT;
    Str_t ref = STR_INIT;
    Str_t list = STR_INIT;
    Str_t push = STR_INIT;
    size_t cap = 0U;
    size_t nc = 0U;
    size_t n = 0U;
    size_t i;
    size_t k;
    size_t c;
    unsigned idle_ms = 0U;
    int self_done = 0;
    int pending;
    int progress;

    memset(&rd, 0, sizeof rd);
    for (i = 0U; i < r->n; i++) {
        if (sel[i] != 0U) {
            cap += str_zcount(&r->v[i].peers);
        }
    }
    chains = xmalloc(((r->n > 0U) ? r->n : 1U) * sizeof *chains);
    chain_of = xmalloc(((r->n > 0U) ? r->n : 1U) * sizeof *chain_of);
    peers = xmalloc(((cap > 0U) ? cap : 1U) * sizeof *peers);

    /* 1. catene e peer: ogni peer che esiste una volta, con i suoi file */
    for (i = 0U; i < r->n; i++) {
        e = &r->v[i];
        chain_of[i] = r->n;
        if ((sel[i] == 0U) || (e->peers.len == 0U)) {
            continue;
        }
        memset(&chains[nc], 0, sizeof chains[nc]);
        memcpy(chains[nc].id, e->id, sizeof chains[nc].id);
        chain_of[i] = nc;
        for (p = str_znext(&e->peers, NULL); p != NULL; p = str_znext(&e->peers, p)) {
            if (!peers_repo_exists(p)) {
                msg_warning(MSG_WARN_PEER_UNREACHABLE, str_cstr(&e->path), p);
                continue;
            }
            for (k = 0U; (k < n) && !str_eq(&peers[k].root, p); k++) {
            }
            if (k == n) {
                fp_peer_init(&peers[n++], p);
            }
            fp_peer_add(&peers[k], nc, e->id);
        }
        nc++;
    }

    /* 2. una versione annunciata qui: prima la si riceve */
    if (own_notices(r, sel)) {
        o->commit = 1U;
        rc = cmd_update_run(r, b, sel, o, rc, batch_ok);
        self_done = 1;
        if ((*batch_ok == 0) || (refresh(b, batch_ok) != GS_OK)) {
            rc = GS_EXIT_FATAL;
            goto exit;
        }
    }

    /* 3. la T da propagare e la sua catena; classificazione dei peer */
    for (i = 0U; i < r->n; i++) {
        if (chain_of[i] < r->n) {
            chain_ref(&ref, r->v[i].id);
            if (git_batch_query(b, str_cstr(&ref), &chains[chain_of[i]].t) != GIT_OBJ_COMMIT) {
                chains[chain_of[i]].t.len = 0U;
            }
        }
    }
    if ((n > 0U) && (fp_reader_open(&rd, peers, n) == GS_OK)) {
        for (c = 0U; c < nc; c++) {
            fp_chain_load(&rd, &chains[c]);
        }
        for (k = 0U; k < n; k++) {
            fp_classify(&rd, &peers[k], chains, str_cstr(&r->objfmt));
        }
    } else {
        for (k = 0U; k < n; k++) {
            peers[k].kind = FPP_FALLBACK;
        }
    }

    /* 4. i peer di ripiego: l'avviso di T (un avviso perso non conta), poi
     * il figlio, subito */
    if (sys_self_exe(&exe) != GS_OK) {
        str_reset(&exe);
        str_add(&exe, "git-shared"); /* dal PATH */
    }
    for (i = 0U; i < r->n; i++) {
        c = chain_of[i];
        if ((c >= r->n) || (chains[c].t.len == 0U)) {
            continue;
        }
        str_reset(&list);
        for (k = 0U; k < n; k++) {
            if ((peers[k].kind == FPP_FALLBACK) && (strstr(str_cstr(&peers[k].ids), r->v[i].id) != NULL)) {
                str_addz(&list, str_cstr(&peers[k].root));
            }
        }
        if (list.len > 0U) {
            str_reset(&push);
            inbox_notify(str_cstr(&r->v[i].path), r->v[i].id, &list, str_cstr(&r->top),
                         &chains[c].t, &tally, &push);
            if (push.len > 0U) {
                peers_push(str_cstr(&r->v[i].path), r->v[i].id, &push, &tally);
            }
        }
    }
    for (k = 0U; k < n; k++) {
        if (peers[k].kind == FPP_FALLBACK) {
            start_child(&peers[k], &exe, o);
        }
    }

    /* 5. percorso veloce, e intanto questa repo */
    fp_status_start(peers, n);
    if (!self_done) {
        o->commit = 1U;
        rc = cmd_update_run(r, b, sel, o, rc, batch_ok);
        (void)fflush(stdout);
    }
    fp_status_finish(peers, n);
    for (k = 0U; k < n; k++) {
        if ((peers[k].kind == FPP_FALLBACK) && !peers[k].started && !peers[k].done &&
            (peers[k].out.len == 0U)) {
            /* lo status ha trovato qualcosa da far decidere al figlio */
            for (i = 0U; i < peers[k].n; i++) {
                (void)inbox_add(str_cstr(&peers[k].root), chains[peers[k].f[i].chain].id,
                                &chains[peers[k].f[i].chain].t, str_cstr(&r->top));
            }
            start_child(&peers[k], &exe, o);
        }
    }
    fp_apply(&rd, peers, n, chains, o->message);
    fp_reader_close(&rd);

    /* 6. righe: i peer veloci nell'ordine, poi i figli appena finiscono */
    for (k = 0U; k < n; k++) {
        if ((peers[k].kind == FPP_FAST) || (peers[k].kind == FPP_OFF)) {
            print_peer(r, &peers[k], &peers[k].out, &rc);
        } else if (!peers[k].started && !peers[k].done) {
            print_peer(r, &peers[k], &peers[k].out, &rc); /* figlio non avviato */
        } else {
            /* figlio: sotto */
        }
    }
    do {
        pending = 0;
        progress = 0;
        for (k = 0U; k < n; k++) {
            if (peers[k].started && !peers[k].done) {
                if (proc_exited(&peers[k].p)) {
                    collect_child(r, &peers[k], &rc);
                    progress = 1;
                } else {
                    pending = 1;
                }
            }
        }
        if (pending && !progress) {
            sys_sleep_ms(2U);
            idle_ms += 2U;
            /* rete di sicurezza: un figlio fermo a pipe piena si sblocca
             * leggendolo */
            for (k = 0U; (k < n) && (idle_ms >= 5000U); k++) {
                if (peers[k].started && !peers[k].done) {
                    collect_child(r, &peers[k], &rc);
                    idle_ms = 0U;
                }
            }
        } else {
            idle_ms = 0U;
        }
    } while (pending);
    if (n == 0U) {
        puts(msg(MSG_UPD_PEERS_NONE));
    }

exit:
    for (k = 0U; k < n; k++) {
        if (peers[k].started && !peers[k].done) {
            collect_child(r, &peers[k], &rc);
        }
        fp_peer_free(&peers[k]);
    }
    for (c = 0U; c < nc; c++) {
        fp_chain_free(&chains[c]);
    }
    free(peers);
    free(chain_of);
    free(chains);
    str_free(&push);
    str_free(&list);
    str_free(&ref);
    str_free(&exe);
    return rc;
}

int cmd_update(int argc, char **argv)
{
    CmdOpts_t o;
    Repo_t repo = REPO_INIT;
    GitBatch_t batch;
    unsigned char *sel = NULL;
    const char *repo_dir = NULL; /* <repo>, se il primo argomento e' una cartella */
    const char *path = NULL;     /* <path> */
    size_t i;
    int aborted = 0;
    int chosen = 0;
    int first;
    int npos;
    int maxpos = 1;
    int opened = 0;
    int batch_ok = 0;
    int rc = GS_EXIT_USAGE;

    first = cmd_parse(argc, argv,
                      CMD_OPT_COMMIT | CMD_OPT_FORCE | CMD_OPT_ABORT | CMD_OPT_SCAN |
                          CMD_OPT_CHOOSE | CMD_OPT_PEERS,
                      MSG_USAGE_UPDATE, &o);
    if (first < 0) {
        goto exit;
    }
    if (o.as_peer != 0U) {
        /* figlio di update --peers: stdout e stderr vanno nella stessa
         * pipe, e senza buffer le righe restano in ordine (D-115) */
        (void)setvbuf(stdout, NULL, _IONBF, 0U);
        o.peers = 0U;
    }
    /* <repo> (D-115): una cartella; un file shared non lo e' mai. */
    npos = argc - first;
    if ((npos > 0) && sys_is_dir(argv[first])) {
        repo_dir = argv[first];
        maxpos = 2;
        if (npos > 1) {
            path = argv[first + 1];
        }
    } else if (npos > 0) {
        path = argv[first];
    } else {
        /* nessun argomento: tutti i file di questa repo */
    }
    if ((npos > maxpos) || ((o.as_peer != 0U) && (path != NULL))) {
        msg_error(MSG_ERR_TOO_MANY_ARGS);
        fputs(msg(MSG_USAGE_UPDATE), stderr);
        goto exit;
    }
    if (((o.ours != 0U) || (o.theirs != 0U)) &&
        (((o.ours != 0U) && (o.theirs != 0U)) || (o.abort != 0U) ||
         (o.force != 0U) || (o.scan != 0U) || (o.peers != 0U))) {
        msg_error(MSG_ERR_CHOOSE_OPTS);
        fputs(msg(MSG_USAGE_UPDATE), stderr);
        goto exit;
    }
    if ((o.abort != 0U) && ((o.commit != 0U) || (o.force != 0U) ||
                            (o.scan != 0U) || (o.peers != 0U))) {
        msg_error(MSG_ERR_ABORT_OPTS);
        fputs(msg(MSG_USAGE_UPDATE), stderr);
        goto exit;
    }
    if ((o.peers != 0U) && ((o.force != 0U) || (repo_dir != NULL))) {
        msg_error(MSG_ERR_PEERS_OPTS);
        fputs(msg(MSG_USAGE_UPDATE), stderr);
        goto exit;
    }
    if ((repo_dir != NULL) && (sys_chdir(repo_dir) != 0)) {
        msg_error(MSG_ERR_CHDIR, repo_dir);
        rc = GS_EXIT_ERROR;
        goto exit;
    }
    rc = cmd_open(&repo, &batch);
    if (rc != GS_EXIT_OK) {
        goto exit;
    }
    opened = 1;
    batch_ok = 1;
    sel = xmalloc((repo.n > 0U) ? repo.n : 1U);

    if (o.as_peer != 0U) {
        rc = peer_run(&repo, &batch, sel, &o, &batch_ok);
        goto exit;
    }
    if (!cmd_select(&repo, path, sel)) {
        rc = GS_EXIT_ERROR;
        goto exit;
    }

    if (o.abort != 0U) {
        for (i = 0U; i < repo.n; i++) {
            if (sel[i] != 0U) {
                abort_entry(&repo, &repo.v[i], &batch, (path != NULL), &rc,
                            &aborted);
            }
        }
        if ((aborted == 0) && (path == NULL)) {
            msg_error(MSG_ERR_NO_MERGE_ANY);
            rc = GS_EXIT_ERROR;
        }
        goto exit;
    }

    /* --ours / --theirs: prima la scelta nell'index, poi la conclusione
     * come in ogni update, solo per i file con un'unione in corso. */
    if ((o.ours != 0U) || (o.theirs != 0U)) {
        for (i = 0U; i < repo.n; i++) {
            if ((sel[i] != 0U) &&
                !choose_entry(&repo, &repo.v[i], &batch, &o, (path != NULL),
                              &rc, &chosen)) {
                sel[i] = 0U;
            }
        }
        if ((chosen == 0) && (path == NULL)) {
            msg_error(MSG_ERR_NO_MERGE_ANY);
            rc = GS_EXIT_ERROR;
        }
        if (refresh(&batch, &batch_ok) != GS_OK) {
            rc = GS_EXIT_FATAL;
            goto exit;
        }
    }

    if (o.peers != 0U) {
        rc = update_peers(&repo, &batch, sel, &o, rc, &batch_ok);
    } else {
        rc = cmd_update_run(&repo, &batch, sel, &o, rc, &batch_ok);
    }

exit:
    free(sel);
    if (opened != 0) {
        if (batch_ok != 0) {
            git_batch_close(&batch);
        }
        repo_close(&repo);
    }
    return rc;
}
