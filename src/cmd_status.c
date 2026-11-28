/*
 * cmd_status.c - "git shared status" (REQ-F-012).
 *
 * Processi git nel caso comune (file allineato), su qualunque numero di
 * file: tre in parallelo (radice, branch, config locale), poi .gitshared
 * e un solo cat-file per tutte le domande. La storia di un file si legge
 * solo quando H e T differiscono.
 *
 * Prima di ricavare lo stato si riceve (receive.h, D-099): senza avvisi
 * nessun processo; con avvisi un fetch per repo annunciante, una volta.
 * Per un file con creatore (remoto) conta anche l'ultima versione vista
 * del creatore, senza rete; la riga "creator" dice da dove arrivano le
 * versioni e com'e' T rispetto al creatore (D-110). Con --remote, prima
 * un fetch per creatore: l'unico caso in cui status va in rete.
 *
 * Gli eredi (D-114): la riga "heirs" li mostra nell'ordine di successione,
 * senza rete. Con --remote, se l'autorita' non risponde, la visita degli
 * eredi dice chi ha preso il suo posto (pull lo segue); se l'autorita' e'
 * un sovrano, anche se il creatore e' tornato.
 *
 * --porcelain (D-119): le stesse informazioni per un programma, una riga
 * "<tipo> <id> <campi>" ciascuna, con parole chiave fisse in inglese, oid
 * completi e date in secondi dal 1970; al piu' un campo libero (nome, path,
 * URL), sempre l'ultimo. Con -z ogni riga finisce con NUL. Il calcolo e'
 * lo stesso: cambia solo la stampa.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmd.h"
#include "cmd_common.h"
#include "chain.h"
#include "err.h"
#include "git.h"
#include "heirs.h"
#include "mem.h"
#include "merge.h"
#include "msg.h"
#include "inbox.h"
#include "peers.h"
#include "receive.h"
#include "remote.h"
#include "shared.h"
#include "state.h"
#include "sys.h"

/* --porcelain (D-119): 1 se attivo, e il carattere di fine riga ('\n', NUL
 * con -z). Valgono per un solo comando, impostati da cmd_status. */
static int s_porc = 0;
static int s_eol = '\n';

/* Una nota per ogni repo dell'elenco (compatto); kw: tipo della riga di
 * --porcelain. Un avviso di una repo che non e' un peer (stranger): la
 * versione arriva solo se la porta 'update --peers' lanciato da lei. */
static void print_notes(const SharedEntry_t *e, const Str_t *from, eMsgId_t what,
                        const char *kw)
{
    const char *p;

    for (p = str_znext(from, NULL); p != NULL; p = str_znext(from, p)) {
        if (s_porc != 0) {
            cmd_porc(s_eol, kw, e->id, "%s", p);
            continue;
        }
        printf("    %-6s ", msg(MSG_STATUS_FIELD_NOTE));
        printf(msg(what), ((*p != '\0') || (what == MSG_STATUS_STRANGER)) ? p : "?");
        putchar('\n');
    }
}

/* alt_path: path del file sul branch che lo dichiara, per un file non
 * dichiarato su questo (STATE_ABSENT); NULL altrimenti. */
static void print_entry(const Repo_t *r, const SharedEntry_t *e, eState_t st,
                        int suspended, const char *alt_path)
{
    if (e->in_gitshared) {
        printf("%s\n", str_cstr(&e->name));
        printf("    %-6s %s\n", msg(MSG_STATUS_FIELD_PATH), str_cstr(&e->path));
    } else if ((st == STATE_PENDING) && (e->pending_path.len > 0U)) {
        /* attesa nata su un altro branch: qui .gitshared non la dichiara */
        printf("%s\n", str_cstr(&e->pending_path));
        printf("    %-6s %s\n", msg(MSG_STATUS_FIELD_PATH),
               str_cstr(&e->pending_path));
    } else if (alt_path != NULL) {
        printf("%s\n", alt_path);
        printf("    %-6s %s\n", msg(MSG_STATUS_FIELD_PATH), alt_path);
    } else {
        printf("%s\n", msg(MSG_STATUS_UNDECLARED));
    }
    printf("    %-6s %s\n", msg(MSG_STATUS_FIELD_ID), e->id);
    printf("    %-6s ", msg(MSG_STATUS_FIELD_STATE));
    if (suspended != 0) {
        printf(msg(MSG_STATUS_SUSPENDED), state_name(st));
    } else if ((st == STATE_PENDING) && (e->pending_branch.len > 0U) &&
               !str_eq(&r->branch, str_cstr(&e->pending_branch))) {
        /* il commit di conferma si aspetta su quel branch (D-077) */
        printf(msg(MSG_STATUS_PENDING_ON), state_name(st),
               str_cstr(&e->pending_branch));
    } else {
        fputs(state_name(st), stdout);
    }
    putchar('\n');
}

/* --porcelain: righe file, name, branch, pending-on, head, tip, base
 * (D-119). Il path come in print_entry; vuoto se non c'e'. T si legge qui
 * se state_derive si e' fermato prima (file assente, voce non dichiarata):
 * la catena c'e' lo stesso. */
static void porc_entry(const Repo_t *r, const SharedEntry_t *e, GitBatch_t *bt,
                       eState_t st, int suspended, const char *alt_path,
                       const StateInfo_t *si)
{
    const char *path = "";
    const char *b;
    Str_t name = STR_INIT;
    Oid_t tip = si->tip;
    Oid_t t = si->t;
    char hex[OID_HEX_SIZE];
    char hex2[OID_HEX_SIZE];

    if ((tip.len == 0U) && e->in_config && (e->id[0] != '\0')) {
        chain_ref(&name, e->id);
        if (git_batch_query(bt, str_cstr(&name), &tip) == GIT_OBJ_COMMIT) {
            oid_to_hex(&tip, hex);
            str_reset(&name);
            str_addf(&name, "%s:content", hex);
            if (git_batch_blob(bt, str_cstr(&name), &t) == 0) {
                t.len = 0U;
            }
        } else {
            tip.len = 0U;
        }
    }

    if (e->in_gitshared) {
        path = str_cstr(&e->path);
    } else if ((st == STATE_PENDING) && (e->pending_path.len > 0U)) {
        path = str_cstr(&e->pending_path);
    } else if (alt_path != NULL) {
        path = alt_path;
    } else {
        /* nessun path */
    }
    cmd_porc(s_eol, "file", e->id, "%s %s %s", state_keyword(st),
         (suspended != 0) ? "suspended" : "active", path);
    if (e->in_gitshared) {
        cmd_porc(s_eol, "name", e->id, "%s", str_cstr(&e->name));
    }
    for (b = str_znext(&e->branches, NULL); b != NULL; b = str_znext(&e->branches, b)) {
        cmd_porc(s_eol, "branch", e->id, "%s", b);
    }
    if ((st == STATE_PENDING) && (e->pending_branch.len > 0U) &&
        !str_eq(&r->branch, str_cstr(&e->pending_branch))) {
        cmd_porc(s_eol, "pending-on", e->id, "%s", str_cstr(&e->pending_branch));
    }
    if (si->h.len > 0U) {
        oid_to_hex(&si->h, hex);
        cmd_porc(s_eol, "head", e->id, "%s", hex);
    }
    if ((tip.len > 0U) && (t.len > 0U)) {
        oid_to_hex(&tip, hex);
        oid_to_hex(&t, hex2);
        cmd_porc(s_eol, "tip", e->id, "%s %s", hex, hex2);
    }
    if ((st == STATE_DIVERGED) && (si->base.len > 0U)) {
        oid_to_hex(&si->base, hex);
        cmd_porc(s_eol, "base", e->id, "%s", hex);
    }
    str_free(&name);
}

/*
 * --porcelain (D-119): creator, authority con il confronto e quando e'
 * stata vista (0 = mai), unreachable, succession (e succession-via),
 * creator-back. rel: il confronto calcolato da print_creator.
 */
static void porc_creator(const SharedEntry_t *e, const Received_t *rv, eMsgId_t rel,
                         unsigned char fetched, const HeirVisit_t *visit, int back)
{
    const char *kw = (rel == MSG_CREATOR_SAME)      ? "same"
                     : (rel == MSG_CREATOR_AHEAD)    ? "ahead"
                     : (rel == MSG_CREATOR_BEHIND)   ? "behind"
                     : (rel == MSG_CREATOR_DIVERGED) ? "diverged"
                     : (rel == MSG_CREATOR_MISSING)  ? "missing"
                                                     : "unseen";
    unsigned long seen = ((rv->auth_tip.len > 0U) && (rel != MSG_CREATOR_MISSING))
                             ? e->seen_at
                             : 0UL;

    cmd_porc(s_eol, "creator", e->id, "%s", str_cstr(&rv->creator));
    cmd_porc(s_eol, "authority", e->id, "%s %lu %s", kw, seen, str_cstr(&rv->authority));
    if (fetched == (unsigned char)REMOTE_UNREACHABLE) {
        cmd_porc(s_eol, "unreachable", e->id, "%s", str_cstr(&rv->authority));
    }
    if ((fetched == (unsigned char)REMOTE_UNREACHABLE) ||
        (fetched == (unsigned char)REMOTE_MISSING)) {
        if (visit->kind == HEIR_VISIT_FOUND) {
            cmd_porc(s_eol, "succession", e->id, "found %u %s", visit->pos, str_cstr(&visit->to));
        } else if (visit->kind == HEIR_VISIT_OTHER) {
            cmd_porc(s_eol, "succession", e->id, "other 0 %s", str_cstr(&visit->to));
        } else if (visit->kind == HEIR_VISIT_CREATOR) {
            cmd_porc(s_eol, "succession", e->id, "creator 0 %s", str_cstr(&visit->where));
        } else {
            cmd_porc(s_eol, "succession", e->id, "none 0 ");
        }
        if ((visit->kind == HEIR_VISIT_FOUND) || (visit->kind == HEIR_VISIT_OTHER)) {
            cmd_porc(s_eol, "succession-via", e->id, "%s", str_cstr(&visit->where));
        }
        /* AB-07: quanti indirizzi della linea hanno risposto, su quanti */
        cmd_porc(s_eol, "succession-answer", e->id, "%u %u", visit->answered,
                 visit->line);
    }
    if (back != 0) {
        cmd_porc(s_eol, "creator-back", e->id, "%s", str_cstr(&rv->creator));
    }
}

/*
 * Riga "creator <url> (<rispetto al creatore>)" (D-110): T confrontata con
 * l'ultima versione vista. Un processo solo se le due sono diverse. Con un
 * sovrano (D-111) la riga "creator" dice solo l'URL e il confronto passa
 * alla riga "authority <url> (...)". fetched: esito di status --remote per
 * il file (CMD_NO_CREATOR = non chiesto).
 */
static void print_creator(const SharedEntry_t *e, GitBatch_t *b,
                          const Received_t *rv, unsigned char fetched,
                          const HeirVisit_t *visit, int back)
{
    const char *path = str_cstr(&e->path);
    const Oid_t *seen = &rv->auth_tip;
    Str_t ref = STR_INIT;
    char date[32];
    eMsgId_t rel;
    Oid_t t;

    chain_ref(&ref, e->id);
    if (git_batch_query(b, str_cstr(&ref), &t) != GIT_OBJ_COMMIT) {
        t.len = 0U;
    }
    if (fetched == (unsigned char)REMOTE_MISSING) {
        rel = MSG_CREATOR_MISSING;
    } else if (seen->len == 0U) {
        rel = MSG_CREATOR_UNSEEN;
    } else if (oid_eq(seen, &t)) {
        rel = MSG_CREATOR_SAME;
    } else if ((t.len > 0U) && merge_is_ancestor(seen, &t)) {
        rel = MSG_CREATOR_AHEAD;
    } else if ((t.len > 0U) && merge_is_ancestor(&t, seen)) {
        rel = MSG_CREATOR_BEHIND;
    } else {
        rel = MSG_CREATOR_DIVERGED;
    }
    if (s_porc != 0) {
        porc_creator(e, rv, rel, fetched, visit, back);
        str_free(&ref);
        return;
    }
    if (rv->sovereign != 0) {
        printf("    %-6s %s\n", msg(MSG_STATUS_FIELD_CREATOR), str_cstr(&rv->creator));
    }
    printf("    %-6s %s (", msg((rv->sovereign != 0) ? MSG_STATUS_FIELD_AUTHORITY
                                                    : MSG_STATUS_FIELD_CREATOR),
           str_cstr(&rv->authority));
    printf(msg(rel), auth_role(rv->sovereign));
    /* Quando e' stata vista (D-112, B-53): senza, "same version" dopo
     * giorni senza rete sembrerebbe un controllo appena fatto. */
    if ((seen->len > 0U) && (rel != MSG_CREATOR_MISSING) && (e->seen_at > 0UL)) {
        fputs(", ", stdout);
        printf(msg(MSG_CREATOR_SEEN), cmd_local_date(e->seen_at, date, sizeof date));
    }
    fputs(")\n", stdout);
    if (fetched == (unsigned char)REMOTE_UNREACHABLE) {
        printf("    %-6s ", msg(MSG_STATUS_FIELD_NOTE));
        printf(msg(MSG_STATUS_CREATOR_UNREACHABLE), auth_role(rv->sovereign),
               str_cstr(&rv->authority));
        putchar('\n');
    }
    /* Chi prende il posto dell'autorita' muta (D-114): si dice soltanto.
     * Lo segue pull, e solo se e' un erede. */
    if ((fetched == (unsigned char)REMOTE_UNREACHABLE) ||
        (fetched == (unsigned char)REMOTE_MISSING)) {
        printf("    %-6s ", msg(MSG_STATUS_FIELD_NOTE));
        if (visit->kind == HEIR_VISIT_FOUND) {
            printf(msg(MSG_STATUS_HEIR_FOUND), str_cstr(&visit->where), visit->pos,
                   str_cstr(&visit->to), path);
        } else if (visit->kind == HEIR_VISIT_OTHER) {
            printf(msg(MSG_STATUS_HEIR_OTHER), str_cstr(&visit->where),
                   str_cstr(&visit->to), str_cstr(&visit->to), path);
        } else if (visit->kind == HEIR_VISIT_CREATOR) {
            printf(msg(MSG_STATUS_CREATOR_BACK), str_cstr(&visit->where), path);
        } else if (visit->line == 0U) {
            printf(msg(MSG_STATUS_HEIRS_EMPTY), path, path);
        } else if (visit->answered == 0U) {
            printf(msg(MSG_STATUS_HEIRS_SILENT), visit->line, path, path);
        } else {
            printf(msg(MSG_STATUS_HEIRS_STILL), visit->answered, visit->line, path,
                   path);
        }
        putchar('\n');
        if ((fetched == (unsigned char)REMOTE_MISSING) &&
            (visit->kind != HEIR_VISIT_OTHER)) {
            /* AB-07: l'autorita' risponde, ma il file non c'e' piu' */
            printf("    %-6s ", msg(MSG_STATUS_FIELD_NOTE));
            printf(msg(MSG_STATUS_AUTH_NO_FILE), auth_role(rv->sovereign));
            putchar('\n');
        }
    }
    if (back != 0) {
        printf("    %-6s ", msg(MSG_STATUS_FIELD_NOTE));
        printf(msg(MSG_STATUS_CREATOR_BACK), str_cstr(&rv->creator), path);
        putchar('\n');
    }
    str_free(&ref);
}

/* Riga "heirs" (D-114): gli eredi nell'ordine di successione, numerati,
 * dalla catena di T. Nessuna riga senza eredi. */
static void print_heirs(const SharedEntry_t *e, GitBatch_t *b)
{
    Auth_t a = AUTH_INIT;
    Heirs_t h = HEIRS_INIT;
    Str_t order = STR_INIT;
    Str_t ref = STR_INIT;
    const char *p;
    unsigned k = 1U;
    Oid_t t;

    chain_ref(&ref, e->id);
    if ((git_batch_query(b, str_cstr(&ref), &t) == GIT_OBJ_COMMIT) &&
        (heirs_of(b, &t, &a, NULL, &h) == GS_OK) && (h.n > 0U)) {
        heirs_order(&a, &h, &order);
    }
    for (p = str_znext(&order, NULL); p != NULL; p = str_znext(&order, p), k++) {
        if (s_porc != 0) {
            cmd_porc(s_eol, "heir", e->id, "%u %s", k, p);
        } else {
            printf("    %-6s %u %s\n", (k == 1U) ? msg(MSG_STATUS_FIELD_HEIRS) : "", k, p);
        }
    }
    str_free(&ref);
    str_free(&order);
    heirs_free(&h);
    auth_free(&a);
}

/* Parola chiave di --porcelain per lo stato di un peer (D-119). */
static const char *peer_keyword(eMsgId_t what)
{
    return (what == MSG_PEER_SAME)        ? "same"
           : (what == MSG_PEER_BEHIND)    ? "behind"
           : (what == MSG_PEER_NEWER)     ? "newer"
           : (what == MSG_PEER_NO_VERSION) ? "none"
           : (what == MSG_PEER_ANNOUNCED) ? "announced"
           : (what == MSG_PEER_LEFT)      ? "left"
           : (what == MSG_PEER_GONE)      ? "gone"
                                          : "unreachable";
}

/*
 * --peers (REQ-F-012, D-086): per ogni peer, se c'e', se condivide ancora
 * il file e com'e' la sua versione rispetto a T. Nessun fetch: si legge la
 * sua refs/shared/<id> e si confronta con la catena locale. Un peer
 * indietro che ha gia' l'avviso di T ricevera' da solo (D-099).
 */
static void print_peers(const SharedEntry_t *e, GitBatch_t *b)
{
    size_t n = str_zcount(&e->peers);
    uint8_t *state = xmalloc((n > 0U) ? n : 1U);
    Oid_t *tips = xmalloc(((n > 0U) ? n : 1U) * sizeof *tips);
    Str_t ref = STR_INIT;
    const char *p;
    char hex[OID_HEX_SIZE];
    Oid_t t;
    Oid_t have;
    eMsgId_t what;
    size_t k;

    if (s_porc == 0) {
        printf("    %-6s ", msg(MSG_STATUS_FIELD_PEERS));
    }
    if (n == 0U) {
        if (s_porc == 0) {
            puts(msg(MSG_BRANCH_NONE));
        }
        goto exit;
    }
    peers_check(e->id, &e->peers, state);
    peers_tips(e->id, &e->peers, tips);
    chain_ref(&ref, e->id);
    if (git_batch_query(b, str_cstr(&ref), &t) != GIT_OBJ_COMMIT) {
        t.len = 0U;
    }
    for (k = 0U, p = str_znext(&e->peers, NULL); p != NULL;
         k++, p = str_znext(&e->peers, p)) {
        if (state[k] == PEER_GONE) {
            what = MSG_PEER_GONE;
        } else if (state[k] == PEER_LEFT) {
            what = MSG_PEER_LEFT;
        } else if (state[k] == PEER_UNREACHABLE) {
            what = MSG_PEER_UNREACHABLE;
        } else if (tips[k].len == 0U) {
            what = MSG_PEER_NO_VERSION;
        } else if (oid_eq(&tips[k], &t)) {
            what = MSG_PEER_SAME;
        } else {
            /* Il peer ha una versione che qui non c'e', o che non discende
             * da quella del peer: serve update. Se e' un antenato di T, al
             * peer manca l'ultima versione: serve publish. */
            oid_to_hex(&tips[k], hex);
            what = ((t.len > 0U) &&
                    (git_batch_query(b, hex, &have) == GIT_OBJ_COMMIT) &&
                    merge_is_ancestor(&tips[k], &t))
                       ? MSG_PEER_BEHIND
                       : MSG_PEER_NEWER;
        }
        /* Avviso di T gia' scritto nel peer: ricevera' da solo (D-099). */
        if (((what == MSG_PEER_BEHIND) || (what == MSG_PEER_NO_VERSION)) &&
            (t.len > 0U) && inbox_has(p, e->id, &t)) {
            what = MSG_PEER_ANNOUNCED;
        }
        if (s_porc != 0) {
            cmd_porc(s_eol, "peer", e->id, "%s %s", peer_keyword(what), p);
            continue;
        }
        if (k > 0U) {
            printf("    %-6s ", "");
        }
        printf("%s: %s\n", p, msg(what));
    }

exit:
    str_free(&ref);
    free(tips);
    free(state);
}

/*
 * --porcelain: cambi di autorita' da decidere (D-111, D-119), con la
 * versione che li porta. Niente autore e data (un git log a testa: chi
 * legge li chiede a git con l'oid) e niente comando per decidere.
 */
static void porc_auth_changes(const SharedEntry_t *e, const Received_t *rv)
{
    const AuthChange_t *c;
    const char *tab;
    char hex[OID_HEX_SIZE];
    size_t k;

    for (k = 0U; k < rv->nchanges; k++) {
        c = &rv->changes[k];
        if (c->rejected != 0U) {
            continue; /* gia' deciso */
        }
        oid_to_hex(&c->version, hex);
        cmd_porc(s_eol, "auth-change", e->id, "%s %s %s", hex,
             (c->by_authority != 0U) ? "authority"
             : (c->from.len > 0U)    ? "peer"
                                     : "unknown",
             str_cstr(&c->authority));
        if ((c->by_authority == 0U) && (c->from.len > 0U)) {
            cmd_porc(s_eol, "auth-change-from", e->id, "%s %s", hex, str_cstr(&c->from));
        }
        tab = strchr(str_cstr(&c->key), '\t');
        if ((c->creator != 0U) && (tab != NULL)) {
            cmd_porc(s_eol, "auth-change-creator", e->id, "%s %s", hex, tab + 1);
        }
    }
}

/* --porcelain: intestazione (D-119). */
static void porc_header(const Repo_t *r)
{
    printf("# porcelain 1%c", s_eol);
    printf("# repo %s%c", str_cstr(&r->top), s_eol);
    if (r->branch.len > 0U) {
        printf("# branch %s%c", str_cstr(&r->branch), s_eol);
    } else {
        printf("# detached%c", s_eol);
    }
}

int cmd_status(int argc, char **argv)
{
    Repo_t repo = REPO_INIT;
    GitBatch_t batch;
    BranchNames_t names = BRANCHNAMES_INIT;
    Str_t list = STR_INIT;
    Str_t elsewhere = STR_INIT; /* branch che dichiara il file (B-16) */
    Str_t alt_path = STR_INIT;
    Str_t chain = STR_INIT;      /* refs/shared/<id> (B-51) */
    Oid_t tip;
    Str_t *urls = NULL;          /* --remote: creatori */
    unsigned char *sel = NULL;   /* --remote: file da chiedere */
    unsigned char *fetched = NULL; /* --remote: esito per file */
    HeirVisit_t *visits = NULL;  /* --remote: eredi di un'autorita' muta */
    int *back = NULL;            /* --remote: il creatore e' tornato */
    const SharedEntry_t *e;
    eState_t st;
    size_t i;
    size_t shown = 0U;
    int batch_open = 0;
    int found;
    int suspended;
    int with_peers = 0;
    int with_remote = 0;
    int received;
    Received_t rv = RECEIVED_INIT;
    StateInfo_t si;
    int rc = GS_EXIT_FATAL;
    eGsErr_t err;

    s_porc = 0;
    s_eol = '\n';
    for (i = 1U; i < (size_t)argc; i++) {
        if (strcmp(argv[i], "--peers") == 0) {
            with_peers = 1;
        } else if (strcmp(argv[i], "--remote") == 0) {
            with_remote = 1;
        } else if ((strcmp(argv[i], "--porcelain") == 0) ||
                   (strcmp(argv[i], "--porcelain=1") == 0)) {
            s_porc = 1;
        } else if (strcmp(argv[i], "-z") == 0) {
            s_porc = 1; /* come git status -z */
            s_eol = '\0';
        } else {
            if (argv[i][0] == '-') {
                msg_error(MSG_ERR_UNKNOWN_OPT, argv[i]);
            } else {
                msg_error(MSG_ERR_TOO_MANY_ARGS);
            }
            fputs(msg(MSG_USAGE_STATUS), stderr);
            rc = GS_EXIT_USAGE;
            goto exit;
        }
    }
    err = repo_open(&repo);
    if (err != GS_OK) {
        msg_fatal((err == GS_ERR_NOT_REPO)  ? MSG_ERR_NOT_REPO
                  : (err == GS_ERR_FORMAT) ? MSG_ERR_READ_GITSHARED
                                           : MSG_ERR_PROC,
                  "git");
        goto exit;
    }
    if (git_batch_open(&batch) != GS_OK) {
        msg_fatal(MSG_ERR_PROC, "git cat-file");
        goto exit;
    }
    batch_open = 1;
    rc = GS_EXIT_OK;
    if (cmd_complete(&repo, &batch, 1) != GS_OK) {
        msg_error(MSG_ERR_PENDING);
        rc = GS_EXIT_ERROR; /* si mostra comunque lo stato */
    }
    repo_check_self(&repo);
    if (s_porc != 0) {
        sys_stdout_binary(); /* solo LF, anche su Windows */
        porc_header(&repo);
    }

    /* --remote (D-110): un fetch per creatore, poi un cat-file che veda le
     * ref e gli oggetti nuovi. Un creatore che non risponde e' una nota. */
    sel = xmalloc((repo.n > 0U) ? repo.n : 1U);
    fetched = xmalloc((repo.n > 0U) ? repo.n : 1U);
    urls = xmalloc(((repo.n > 0U) ? repo.n : 1U) * sizeof *urls);
    visits = xmalloc(((repo.n > 0U) ? repo.n : 1U) * sizeof *visits);
    back = xmalloc(((repo.n > 0U) ? repo.n : 1U) * sizeof *back);
    for (i = 0U; i < repo.n; i++) {
        e = &repo.v[i];
        str_init(&urls[i]);
        str_init(&visits[i].where);
        str_init(&visits[i].to);
        visits[i].kind = HEIR_VISIT_NONE;
        visits[i].pos = 0U;
        visits[i].line = 0U;
        visits[i].answered = 0U;
        back[i] = 0;
        fetched[i] = CMD_NO_CREATOR;
        sel[i] = (with_remote && e->in_config && e->valid &&
                  (e->pending == SHARED_PENDING_NONE) && (e->id[0] != '\0'))
                     ? 1U
                     : 0U;
    }
    if (with_remote) {
        cmd_fetch_creators(&repo, &batch, sel, fetched, urls, NULL);
        git_batch_close(&batch);
        batch_open = 0;
        if (git_batch_open(&batch) != GS_OK) {
            msg_fatal(MSG_ERR_PROC, "git cat-file");
            rc = GS_EXIT_FATAL;
            goto exit;
        }
        batch_open = 1;
        /* Autorita' muta: gli eredi; sovrano che risponde: il creatore
         * (D-114). Ognuno riapre il cat-file. */
        for (i = 0U; i < repo.n; i++) {
            if ((sel[i] == 0U) || (urls[i].len == 0U)) {
                continue;
            }
            if ((fetched[i] == (unsigned char)REMOTE_UNREACHABLE) ||
                (fetched[i] == (unsigned char)REMOTE_MISSING)) {
                batch_open = cmd_heirs_visit(&repo, &repo.v[i], &batch, 0, &visits[i]);
            } else if (fetched[i] == (unsigned char)REMOTE_GOT) {
                batch_open = cmd_creator_back(&repo, &repo.v[i], &batch, &back[i]);
            } else {
                /* nessun creatore */
            }
            if (batch_open == 0) {
                rc = GS_EXIT_FATAL;
                goto exit;
            }
        }
    }

    for (i = 0U; i < repo.n; i++) {
        e = &repo.v[i];
        if (e->in_gitshared && !e->valid) {
            msg_warning(MSG_WARN_BAD_ENTRY, str_cstr(&e->name));
            if (s_porc != 0) {
                cmd_porc(s_eol, "invalid", e->id, "%s", str_cstr(&e->name));
            }
        } else if (e->in_gitshared || e->in_config) {
            /* Prima si riceve (D-099): lo stato e' quello giusto, non
             * "allineato" con una nota. Senza avvisi resta tutto locale.
             * Una catena assente la segnala lo stato. */
            received = e->in_config && (e->pending == SHARED_PENDING_NONE) &&
                       (e->id[0] != '\0') &&
                       (receive_entry(&repo, e, &batch, 0, &rv) == GS_OK);
            if (state_derive(&repo, e, &batch, &st, &si) != GS_OK) {
                msg_fatal(MSG_ERR_PROC, "git");
                rc = GS_EXIT_FATAL;
                goto exit;
            }
            suspended = (state_is_derived(st) &&
                         !shared_is_active(e, str_cstr(&repo.branch))) ||
                        (st == STATE_ABSENT);
            found = 0;
            if ((st == STATE_ABSENT) &&
                (repo_declaring_branch(e->id, &batch, &elsewhere, &alt_path,
                                       &found) != GS_OK)) {
                found = 0; /* solo per i messaggi: senza, niente nota */
            }
            if (s_porc != 0) {
                porc_entry(&repo, e, &batch, st, suspended,
                           (found != 0) ? str_cstr(&alt_path) : NULL, &si);
            } else {
                if (shown > 0U) {
                    putchar('\n');
                }
                print_entry(&repo, e, st, suspended,
                            (found != 0) ? str_cstr(&alt_path) : NULL);
            }
            /* In remoto: da dove arrivano le versioni e com'e' T
             * rispetto al creatore (D-110). */
            if (received && (rv.creator.len > 0U)) {
                print_creator(e, &batch, &rv, fetched[i], &visits[i], back[i]);
                print_heirs(e, &batch);
            }
            /* Cambi di autorita' da decidere (D-111): una riga, non uno
             * stato. */
            if (received && (s_porc != 0)) {
                porc_auth_changes(e, &rv);
            } else if (received) {
                cmd_print_auth_changes(e, &rv, 0);
            } else {
                /* niente ricevuto: nessun cambio */
            }
            /* Catena assente e non ricostruita (D-112, B-51): l'unica
             * uscita e' unlink, poi di nuovo link. */
            if (!received && e->in_config && e->in_gitshared &&
                (e->pending == SHARED_PENDING_NONE) && (st == STATE_INCONSISTENT)) {
                chain_ref(&chain, e->id);
                if (git_batch_query(&batch, str_cstr(&chain), &tip) != GIT_OBJ_COMMIT) {
                    if (s_porc != 0) {
                        cmd_porc(s_eol, "chain-missing", e->id, NULL);
                    } else {
                        printf("    %-6s ", msg(MSG_STATUS_FIELD_NOTE));
                        printf(msg(MSG_STATUS_CHAIN_MISSING), str_cstr(&e->path));
                        putchar('\n');
                    }
                }
            }
            /* Non dichiarato qui: dove lo e' (B-16, D-095). */
            if ((found != 0) && (s_porc != 0)) {
                cmd_porc(s_eol, "elsewhere", e->id, "%s", str_cstr(&elsewhere));
            } else if (found != 0) {
                printf("    %-6s ", msg(MSG_STATUS_FIELD_NOTE));
                printf(msg(MSG_STATUS_ELSEWHERE), str_cstr(&elsewhere));
                putchar('\n');
            } else {
                /* dichiarato qui */
            }
            /* Rinomina di git shared mv non committata (D-101). */
            if ((st == STATE_PENDING) && (e->pending == SHARED_PENDING_NONE) &&
                (state_renamed(&batch, e, &alt_path, &found) == GS_OK) &&
                (found != 0)) {
                if (s_porc != 0) {
                    cmd_porc(s_eol, "renamed", e->id, "%s", str_cstr(&alt_path));
                } else {
                    printf("    %-6s ", msg(MSG_STATUS_FIELD_NOTE));
                    printf(msg(MSG_STATUS_RENAMED), str_cstr(&alt_path));
                    putchar('\n');
                }
            }
            /* Sospeso perche' nessun suo branch esiste piu' (B-21). */
            if (suspended && shared_branches_gone(&names, e, &list)) {
                if (s_porc != 0) {
                    cmd_porc(s_eol, "branches-gone", e->id, NULL);
                } else {
                    printf("    %-6s ", msg(MSG_STATUS_FIELD_NOTE));
                    printf(msg(MSG_STATUS_BRANCHES_GONE), str_cstr(&list),
                           e->in_gitshared ? str_cstr(&e->path) : e->id);
                    putchar('\n');
                }
            }
            /* Avvisi rimasti: versione non arrivata (fetch fallito) o da
             * unire. Durante un'unione lo dice gia' lo stato. */
            if (received) {
                print_notes(e, &rv.missing, MSG_STATUS_ANNOUNCED, "announced");
                print_notes(e, &rv.strangers, MSG_STATUS_STRANGER, "stranger");
                if (st != STATE_MERGING) {
                    print_notes(e, &rv.to_merge, MSG_STATUS_TO_MERGE, "to-merge");
                }
            }
            /* Peer: per i file collegati e non in attesa di set o link. */
            if (with_peers && e->in_config && (e->pending != SHARED_PENDING_SET) &&
                (e->pending != SHARED_PENDING_LINK)) {
                print_peers(e, &batch);
            }
            shown++;
        } else {
            /* attesa appena scartata: niente da mostrare */
        }
    }
    if ((shown == 0U) && (s_porc == 0)) {
        puts(msg(MSG_STATUS_NONE));
    }

exit:
    if (batch_open != 0) {
        git_batch_close(&batch);
    }
    if (urls != NULL) {
        for (i = 0U; i < repo.n; i++) {
            str_free(&urls[i]);
            heirvisit_free(&visits[i]);
        }
    }
    free(back);
    free(visits);
    free(urls);
    free(fetched);
    free(sel);
    repo_close(&repo);
    branchnames_free(&names);
    received_free(&rv);
    str_free(&alt_path);
    str_free(&chain);
    str_free(&elsewhere);
    str_free(&list);
    return rc;
}
