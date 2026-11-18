/*
 * cmd_link.c - "git shared link [-c | -m <msg>] [--] <sorgente> [<dest>]"
 * (REQ-F-002, D-005, D-028, D-059).
 *
 * Al comando: fetch della catena dalla sorgente in una ref temporanea,
 * file scritto dal blob dell'ultima versione, sezione in .gitshared, tutto
 * in staging; operazione in attesa con la radice della sorgente. Al commit
 * (post-commit, D-100; repo_complete_pending): catena, membership,
 * annuncio ai peer, avvisi delle versioni pubblicate nel frattempo.
 *
 * Una sorgente con il set (o il link) ancora in attesa lo completa prima
 * del fetch (D-100): il suo post-commit puo' non essere girato.
 *
 * v0: la destinazione non deve esistere (D-062, riuso della storia, e
 * D-064, --adopt, non sono ancora implementati).
 *
 * Ri-aggancio (D-043, D-079): se .gitshared dichiara gia' lo stesso ID ma
 * la repo non ha dati locali ("scollegato": un clone, o un'attesa scartata
 * durante un git stash), link non da' errore. Riprende nome e path della
 * sezione, rifa' fetch, attesa e hook, e non tocca il file: lo stato si
 * ricava poi come per ogni file shared. Se la sezione e' gia' committata
 * il collegamento vale subito.
 */
#include <stdio.h>
#include <string.h>

#include "cmd.h"
#include "cmd_common.h"
#include "hooks.h"
#include "path.h"
#include "peers.h"
#include "sys.h"

/* La sorgente: repo, path relativo e voce in .gitshared. */
typedef struct {
    Str_t top;
    Str_t rel;
    Str_t objfmt;
    Repo_t shared; /* solo le voci del suo .gitshared */
} Source_t;

static void source_free(Source_t *s)
{
    str_free(&s->top);
    str_free(&s->rel);
    str_free(&s->objfmt);
    repo_close(&s->shared);
}

/* Risale dalla path del file alla repo sorgente e alla sua voce (D-005). */
static const SharedEntry_t *find_source(const Repo_t *r, const char *user_path,
                                        Source_t *s)
{
    Str_t dir = STR_INIT;
    Str_t prefix = STR_INIT;
    Str_t file = STR_INIT;
    const SharedEntry_t *e = NULL;
    const char *name;

    path_split(user_path, &dir, &name);
    if ((name[0] == '\0') ||
        (repo_locate(str_cstr(&dir), &s->top, &prefix, &s->objfmt) != GS_OK) ||
        !path_join_norm(str_cstr(&prefix), name, &s->rel)) {
        msg_error(MSG_ERR_SRC_NOT_REPO, user_path);
        goto exit;
    }
    if (str_eq(&s->top, str_cstr(&r->top))) {
        msg_error(MSG_ERR_SAME_REPO);
        goto exit;
    }
    /* Tutti i peer usano lo stesso algoritmo di hash (REQ-NF-009). */
    if (!str_eq(&s->objfmt, str_cstr(&r->objfmt))) {
        msg_error(MSG_ERR_HASH_MISMATCH, str_cstr(&s->objfmt),
                  str_cstr(&r->objfmt));
        goto exit;
    }
    str_addf(&file, "%s/.gitshared", str_cstr(&s->top));
    if (repo_load_gitshared(&s->shared, str_cstr(&file), 0, 0) == GS_OK) {
        e = repo_find_path(&s->shared, str_cstr(&s->rel));
    }
    if ((e == NULL) || !e->valid) {
        msg_error(MSG_ERR_SRC_NOT_SHARED, user_path); /* D-019 */
        e = NULL;
    }

exit:
    str_free(&file);
    str_free(&prefix);
    str_free(&dir);
    return e;
}

/* 1 se la sorgente ha un'operazione in attesa per l'ID (set o link). */
static int source_pending(const Source_t *s, const char *id)
{
    const char *args[] = { "-C", NULL, "config", "--local", "--get", NULL, NULL };
    Str_t key = STR_INIT;
    Str_t out = STR_INIT;
    int code = 1;

    str_addf(&key, "shared.%s.pending", id);
    args[1] = str_cstr(&s->top);
    args[5] = str_cstr(&key);
    if (git_capture(args, PROC_STDERR_NULL | PROC_CLEAN_ENV, &out, &code) != GS_OK) {
        code = 1;
    }
    str_free(&out);
    str_free(&key);
    return code == 0;
}

/*
 * Sorgente con un set o un link in attesa (D-100): di solito committato,
 * ma nessun comando git shared vi e' girato dopo (hook saltato). Lo
 * completa la sorgente stessa, con le sue regole (D-077), senza scartare
 * nulla. 1 se adesso non e' piu' in attesa.
 */
static int source_settle(const Source_t *s, const char *id)
{
    const char *args[] = { "-C", NULL, "shared", "hook", "complete", NULL };

    if (!source_pending(s, id)) {
        return 1;
    }
    args[1] = str_cstr(&s->top);
    (void)git_run(args, PROC_CLEAN_ENV, NULL); /* i suoi avvisi su stderr */
    return !source_pending(s, id);
}

/* 1 se il path esiste gia' nella destinazione: su disco o nell'index. */
static int dest_exists(const Repo_t *r, GitBatch_t *b, const char *path)
{
    Str_t full = STR_INIT;
    Str_t name = STR_INIT;
    Oid_t oid;
    FILE *f;
    int exists;

    str_addf(&full, "%s/%s", str_cstr(&r->top), path);
    str_addf(&name, ":%s", path); /* ":<path>" = voce dell'index */
    f = sys_fopen(str_cstr(&full), "rb");
    exists = (f != NULL) || sys_is_dir(str_cstr(&full)) ||
             (git_batch_query(b, str_cstr(&name), &oid) != GIT_OBJ_MISSING);
    if (f != NULL) {
        (void)fclose(f);
    }
    str_free(&name);
    str_free(&full);
    return exists;
}

int cmd_link(int argc, char **argv)
{
    CmdOpts_t o;
    Repo_t repo = REPO_INIT;
    GitBatch_t batch;
    Source_t src = { STR_INIT, STR_INIT, STR_INIT, REPO_INIT };
    PeerTally_t tally = { 0U, 0U, 0U };
    const SharedEntry_t *se;
    SharedEntry_t *e;
    Str_t dest = STR_INIT;
    Str_t name = STR_INIT;
    Str_t id = STR_INIT;
    Str_t peers = STR_INIT;
    Str_t text = STR_INIT;
    Str_t paths = STR_INIT;
    Oid_t blob;
    Oid_t head_blob;
    uint8_t fetched = 0U;
    int first;
    int relink = 0;
    int dest_new = 1; /* il file non e' in HEAD: entra nel commit */
    int opened = 0;
    int hooks = 0;
    int rc = GS_EXIT_USAGE;

    first = cmd_parse(argc, argv, CMD_OPT_COMMIT, MSG_USAGE_LINK, &o);
    if (first < 0) {
        goto exit;
    }
    if ((first >= argc) || ((argc - first) > 2)) {
        msg_error((first >= argc) ? MSG_ERR_MISSING_PATH : MSG_ERR_TOO_MANY_ARGS);
        fputs(msg(MSG_USAGE_LINK), stderr);
        goto exit;
    }
    rc = cmd_open(&repo, &batch);
    if (rc != GS_EXIT_OK) {
        goto exit;
    }
    opened = 1;
    rc = GS_EXIT_ERROR;

    /* --- sorgente ----------------------------------------------------- */
    se = find_source(&repo, argv[first], &src);
    if (se == NULL) {
        goto exit;
    }
    str_add(&name, str_cstr(&se->name));
    str_add(&id, se->id);

    /* --- destinazione (D-028) ----------------------------------------- */
    if ((argc - first) == 2) {
        if (!cmd_repo_path(&repo, argv[first + 1], &dest) ||
            (strchr(argv[first + 1], '\n') != NULL)) {
            msg_error(MSG_ERR_DEST_PATH, argv[first + 1]);
            goto exit;
        }
    } else {
        str_add(&dest, str_cstr(&src.rel));
    }
    e = repo_find_id(&repo, str_cstr(&id));
    relink = (e != NULL) && e->in_gitshared && e->valid && !e->in_config;
    if (relink) {
        /* Ri-aggancio: vale la sezione che c'e' (nome e path locali). */
        if (((argc - first) == 2) && !str_eq(&e->path, str_cstr(&dest))) {
            msg_error(MSG_ERR_RELINK_PATH, str_cstr(&e->name),
                      str_cstr(&e->path));
            goto exit;
        }
        str_reset(&dest);
        str_add(&dest, str_cstr(&e->path));
        str_reset(&name);
        str_add(&name, str_cstr(&e->name));
    } else if ((e != NULL) && (e->in_gitshared || e->in_config)) {
        msg_error(MSG_ERR_ALREADY_LINKED, str_cstr(&name), str_cstr(&id));
        goto exit;
    } else {
        e = repo_find_path(&repo, str_cstr(&dest));
        if (e != NULL) {
            msg_error(MSG_ERR_ALREADY_SHARED, str_cstr(&dest), str_cstr(&e->name));
            goto exit;
        }
        if (repo_find_name(&repo, str_cstr(&name)) != NULL) {
            msg_error(MSG_ERR_NAME_USED, str_cstr(&name));
            goto exit;
        }
        if (dest_exists(&repo, &batch, str_cstr(&dest))) {
            msg_error(MSG_ERR_DEST_EXISTS, str_cstr(&dest));
            goto exit;
        }
    }

    /* --- sorgente in attesa: la si completa (D-100) ------------------- */
    if (!source_settle(&src, str_cstr(&id))) {
        msg_error(MSG_ERR_SRC_PENDING, argv[first], str_cstr(&src.top));
        goto exit;
    }

    /* --- catena: fetch in refs/shared-incoming/<id>/0 (D-040) ---------- */
    str_addz(&peers, str_cstr(&src.top));
    peers_fetch(str_cstr(&dest), str_cstr(&id), &peers, &fetched, &tally);
    peers_incoming_ref(&text, str_cstr(&id), 0U);
    str_add(&text, ":content");
    if ((fetched == 0U) || (git_batch_blob(&batch, str_cstr(&text), &blob) == 0)) {
        msg_error(MSG_ERR_SRC_NO_VERSIONS, argv[first], str_cstr(&src.top));
        goto exit;
    }

    /* --- scrittura: attesa per prima (come set) ------------------------- */
    if (cmd_set_pending(&repo, str_cstr(&id), "link", str_cstr(&dest),
                        str_cstr(&src.top), NULL) != GS_OK) {
        msg_error(MSG_ERR_PENDING);
        goto exit;
    }
    if (!relink) {
        if (cmd_write_gitshared(&repo, str_cstr(&name), str_cstr(&id),
                                str_cstr(&dest)) != GS_OK) {
            msg_error(MSG_ERR_WRITE_GITSHARED);
            goto exit;
        }
        if (cmd_write_file(&repo, str_cstr(&dest), &blob) != GS_OK) {
            msg_error(MSG_ERR_WRITE_FILE, str_cstr(&dest));
            goto exit;
        }
    } else {
        /* Il file non si tocca. Se non e' in HEAD (attesa scartata da uno
         * stash, poi stash pop) torna in staging con .gitshared; se c'e',
         * eventuali modifiche dell'utente restano fuori dal commit. */
        str_reset(&text);
        str_addf(&text, "HEAD:%s", str_cstr(&dest));
        dest_new = !git_batch_blob(&batch, str_cstr(&text), &head_blob);
        if (dest_new && (cmd_stage(&repo, str_cstr(&dest)) != GS_OK)) {
            msg_error(MSG_ERR_WRITE_FILE, str_cstr(&dest));
            goto exit;
        }
    }
    if (cmd_stage(&repo, ".gitshared") != GS_OK) {
        msg_error(MSG_ERR_STAGE_GITSHARED);
        goto exit;
    }
    hooks = 1; /* REQ-F-016: installati alla fine, vedi exit */

    if (!relink) {
        e = repo_add(&repo);
        str_add(&e->name, str_cstr(&name));
        str_add(&e->path, str_cstr(&dest));
        memcpy(e->id, str_cstr(&id), GS_ID_HEX_LEN + 1U);
        e->in_gitshared = 1U;
        e->valid = 1U;
    } else {
        e = repo_find_id(&repo, str_cstr(&id));
    }
    str_add(&e->pending_src, str_cstr(&src.top));
    str_add(&e->pending_path, str_cstr(&dest));
    str_add(&e->pending_branch, str_cstr(&repo.branch));
    e->in_config = 1U;
    e->pending = SHARED_PENDING_LINK;

    /* Ri-aggancio con la sezione gia' committata (es. un clone): niente
     * da committare, il collegamento vale subito. */
    if (relink) {
        if (cmd_reopen(&repo, &batch) != GS_EXIT_OK) {
            opened = 0;
            goto exit;
        }
        e = repo_find_id(&repo, str_cstr(&id));
        if ((e != NULL) && (e->pending == SHARED_PENDING_NONE)) {
            printf(msg(MSG_LINK_DONE), str_cstr(&dest), str_cstr(&name),
                   str_cstr(&src.top));
            putchar('\n');
            rc = GS_EXIT_OK;
            goto exit;
        }
    }

    if (o.commit == 0U) {
        printf(msg(MSG_LINK_STAGED), str_cstr(&dest), str_cstr(&name),
               str_cstr(&src.top));
        putchar('\n');
        rc = GS_EXIT_OK;
        goto exit;
    }

    /* --- commit dedicato: .gitshared e il file (D-007) ------------------ */
    str_reset(&text);
    if (o.message == NULL) {
        str_addf(&text, msg(MSG_LINK_COMMIT_MSG), str_cstr(&dest));
        o.message = str_cstr(&text);
    }
    str_addz(&paths, ".gitshared");
    if (dest_new) {
        str_addz(&paths, str_cstr(&dest));
    }
    if (cmd_commit(&repo, o.message, &paths) != GS_OK) {
        msg_error(MSG_ERR_COMMIT);
        goto exit;
    }
    /* Completamento con un cat-file che veda il nuovo HEAD. */
    if (cmd_reopen(&repo, &batch) != GS_EXIT_OK) {
        opened = 0;
        goto exit;
    }
    e = repo_find_path(&repo, str_cstr(&dest));
    if ((e != NULL) && (e->pending == SHARED_PENDING_NONE)) {
        printf(msg(MSG_LINK_DONE), str_cstr(&dest), str_cstr(&name),
               str_cstr(&src.top));
        putchar('\n');
    }
    rc = GS_EXIT_OK;

exit:
    /* Hook alla fine, quando l'attesa e' aperta o gia' chiusa: il
     * post-commit serve solo se resta aperta (D-100). Un errore e' solo un
     * avviso. */
    if ((opened != 0) && (hooks != 0)) {
        (void)hooks_sync(&repo);
    }
    if (opened != 0) {
        git_batch_close(&batch);
        repo_close(&repo);
    }
    source_free(&src);
    str_free(&paths);
    str_free(&text);
    str_free(&peers);
    str_free(&id);
    str_free(&name);
    str_free(&dest);
    return rc;
}
