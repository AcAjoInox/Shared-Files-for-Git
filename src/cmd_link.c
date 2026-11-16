/*
 * cmd_link.c - "git shared link [-c | -m <msg>] [--] <sorgente> [<dest>]"
 * (REQ-F-002, D-005, D-028, D-059).
 *
 * Al comando: fetch della catena dalla sorgente in una ref temporanea,
 * file scritto dal blob dell'ultima versione, sezione in .gitshared, tutto
 * in staging; operazione in attesa con la radice della sorgente. Al commit
 * (repo_complete_pending): catena, membership, annuncio ai peer.
 *
 * v0: la destinazione non deve esistere (D-062, riuso della storia, e
 * D-064, --adopt, non sono ancora implementati).
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
    uint8_t fetched = 0U;
    int first;
    int opened = 0;
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
    if ((e != NULL) && (e->in_gitshared || e->in_config)) {
        msg_error(MSG_ERR_ALREADY_LINKED, str_cstr(&name), str_cstr(&id));
        goto exit;
    }
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
                        str_cstr(&src.top)) != GS_OK) {
        msg_error(MSG_ERR_PENDING);
        goto exit;
    }
    if (cmd_write_gitshared(&repo, str_cstr(&name), str_cstr(&id),
                            str_cstr(&dest)) != GS_OK) {
        msg_error(MSG_ERR_WRITE_GITSHARED);
        goto exit;
    }
    if (cmd_write_file(&repo, str_cstr(&dest), &blob) != GS_OK) {
        msg_error(MSG_ERR_WRITE_FILE, str_cstr(&dest));
        goto exit;
    }
    if (cmd_stage(&repo, ".gitshared") != GS_OK) {
        msg_error(MSG_ERR_STAGE_GITSHARED);
        goto exit;
    }
    (void)hooks_sync(&repo); /* REQ-F-016: un errore e' solo un avviso */

    e = repo_add(&repo);
    str_add(&e->name, str_cstr(&name));
    str_add(&e->path, str_cstr(&dest));
    str_add(&e->pending_src, str_cstr(&src.top));
    str_add(&e->pending_path, str_cstr(&dest));
    str_add(&e->pending_branch, str_cstr(&repo.branch));
    memcpy(e->id, str_cstr(&id), GS_ID_HEX_LEN + 1U);
    e->in_gitshared = 1U;
    e->in_config = 1U;
    e->valid = 1U;
    e->pending = SHARED_PENDING_LINK;

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
    str_addz(&paths, str_cstr(&dest));
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
