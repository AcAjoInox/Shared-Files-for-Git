/*
 * cmd_common.c - parti comuni ai sottocomandi.
 */
#include "cmd_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chain.h"
#include "remote.h"
#include "hooks.h"
#include "mem.h"
#include "path.h"
#include "inbox.h"
#include "peers.h"
#include "receive.h"
#include "sys.h"

static const char gitshared_header[] =
    "# Managed by 'git shared': do not edit by hand.\n"
    "# This repository shares files with other repositories: "
    "run 'git shared status'.\n";

int cmd_parse(int argc, char **argv, unsigned allowed, eMsgId_t usage,
              CmdOpts_t *o)
{
    int i = 1;
    int end_opts = 0;
    int rc = -1;

    o->message = NULL;
    o->commit = 0U;
    o->force = 0U;
    o->dry_run = 0U;
    o->abort = 0U;
    o->all = 0U;
    o->merge = 0U;
    o->adopt = 0U;
    o->ours = 0U;
    o->theirs = 0U;
    o->dashdash = 0U;
    while ((i < argc) && (end_opts == 0) && (argv[i][0] == '-') &&
           (argv[i][1] != '\0')) {
        if (strcmp(argv[i], "--") == 0) {
            end_opts = 1;
            o->dashdash = 1U;
        } else if (((allowed & CMD_OPT_COMMIT) != 0U) &&
                   ((strcmp(argv[i], "-c") == 0) ||
                    (strcmp(argv[i], "--commit") == 0))) {
            o->commit = 1U;
        } else if (((allowed & CMD_OPT_COMMIT) != 0U) &&
                   (strcmp(argv[i], "-m") == 0)) {
            if ((i + 1) >= argc) {
                msg_error(MSG_ERR_OPT_VALUE, argv[i]);
                goto exit;
            }
            i++;
            o->message = argv[i];
            o->commit = 1U;
        } else if (((allowed & CMD_OPT_FORCE) != 0U) &&
                   ((strcmp(argv[i], "-f") == 0) ||
                    (strcmp(argv[i], "--force") == 0))) {
            o->force = 1U;
        } else if (((allowed & CMD_OPT_DRY_RUN) != 0U) &&
                   ((strcmp(argv[i], "-n") == 0) ||
                    (strcmp(argv[i], "--dry-run") == 0))) {
            o->dry_run = 1U;
        } else if (((allowed & CMD_OPT_ABORT) != 0U) &&
                   (strcmp(argv[i], "--abort") == 0)) {
            o->abort = 1U;
        } else if (((allowed & CMD_OPT_ALL) != 0U) &&
                   (strcmp(argv[i], "--all") == 0)) {
            o->all = 1U;
        } else if (((allowed & CMD_OPT_MERGE) != 0U) &&
                   (strcmp(argv[i], "--merge") == 0)) {
            o->merge = 1U;
        } else if (((allowed & CMD_OPT_ADOPT) != 0U) &&
                   (strcmp(argv[i], "--adopt") == 0)) {
            o->adopt = 1U;
        } else if (((allowed & CMD_OPT_CHOOSE) != 0U) &&
                   (strcmp(argv[i], "--ours") == 0)) {
            o->ours = 1U;
        } else if (((allowed & CMD_OPT_CHOOSE) != 0U) &&
                   (strcmp(argv[i], "--theirs") == 0)) {
            o->theirs = 1U;
        } else {
            msg_error(MSG_ERR_UNKNOWN_OPT, argv[i]);
            goto exit;
        }
        i++;
    }
    rc = i;

exit:
    if (rc < 0) {
        fputs(msg(usage), stderr);
    }
    return rc;
}

eGsErr_t cmd_complete(Repo_t *r, GitBatch_t *b, int may_discard)
{
    int had = repo_has_pending(r);
    eGsErr_t err = repo_complete_pending(r, b, may_discard);

    /* L'ultima attesa chiusa: il post-commit non serve piu' (D-100). */
    if (had && !repo_has_pending(r)) {
        (void)hooks_sync_post_commit(r);
    }
    return err;
}

int cmd_open(Repo_t *r, GitBatch_t *b)
{
    int rc = GS_EXIT_FATAL;
    eGsErr_t err = repo_open(r);

    if (err != GS_OK) {
        msg_fatal((err == GS_ERR_NOT_REPO)  ? MSG_ERR_NOT_REPO
                  : (err == GS_ERR_FORMAT) ? MSG_ERR_READ_GITSHARED
                                           : MSG_ERR_PROC,
                  "git");
        repo_close(r);
    } else if (git_batch_open(b) != GS_OK) {
        msg_fatal(MSG_ERR_PROC, "git cat-file");
        repo_close(r);
    } else {
        rc = GS_EXIT_OK;
        if (cmd_complete(r, b, 1) != GS_OK) {
            msg_error(MSG_ERR_PENDING); /* si prosegue: lo stato lo mostra */
        }
        repo_check_self(r);
    }
    return rc;
}

/* "git config shared.<id>.<field> <value>" */
static eGsErr_t set_key(const char *id, const char *field, const char *value)
{
    const char *args[] = { "config", NULL, value, NULL };
    Str_t key = STR_INIT;
    eGsErr_t err;

    str_addf(&key, "shared.%s.%s", id, field);
    args[1] = str_cstr(&key);
    err = git_run(args, 0U, NULL);
    str_free(&key);
    return err;
}

eGsErr_t cmd_set_pending(const Repo_t *r, const char *id, const char *op,
                         const char *path, const char *src, int remote,
                         const char *commit)
{
    /* Una chiave sola, scritta in una volta (D-113): niente attese a
     * meta' (D-077) ne' campi rimasti da un'attesa interrotta (D-106). */
    Str_t value = STR_INIT;
    eGsErr_t err = GS_ERR_USAGE;

    if (shared_pending_value(&value, op, path, str_cstr(&r->branch), src,
                             remote, commit)) {
        err = set_key(id, "pending", str_cstr(&value));
    }
    str_free(&value);
    return err;
}

eGsErr_t cmd_write_gitshared(const Repo_t *r, const char *name,
                             const char *id, const char *path)
{
    const char *args[] = { "config", "-f", NULL, NULL, NULL, NULL, NULL };
    Str_t file = STR_INIT;
    Str_t key = STR_INIT;
    FILE *f;
    int created = 0;
    int put_rc;
    int close_rc;
    eGsErr_t err = GS_ERR_SYS;

    str_addf(&file, "%s/.gitshared", str_cstr(&r->top));
    f = sys_fopen(str_cstr(&file), "rb");
    if (f != NULL) {
        (void)fclose(f);
    } else {
        f = sys_fopen(str_cstr(&file), "wb");
        if (f == NULL) {
            goto exit;
        }
        created = 1;
        put_rc = fputs(gitshared_header, f);
        close_rc = fclose(f);
        if ((put_rc < 0) || (close_rc != 0)) {
            goto exit;
        }
    }

    args[2] = str_cstr(&file);
    str_addf(&key, "shared.%s.id", name);
    args[3] = str_cstr(&key);
    args[4] = id;
    err = git_run(args, 0U, NULL);
    if (err == GS_OK) {
        str_reset(&key);
        str_addf(&key, "shared.%s.path", name);
        args[3] = str_cstr(&key);
        args[4] = path;
        err = git_run(args, 0U, NULL);
    }
    if ((err == GS_OK) && (r->branch.len > 0U)) {
        str_reset(&key);
        str_addf(&key, "shared.%s.branch", name);
        args[3] = "--add";
        args[4] = str_cstr(&key);
        args[5] = str_cstr(&r->branch);
        err = git_run(args, 0U, NULL);
    }
    if ((err != GS_OK) && (created == 0)) {
        str_reset(&key);
        str_addf(&key, "shared.%s", name);
        args[3] = "--remove-section";
        args[4] = str_cstr(&key);
        args[5] = NULL;
        (void)git_run(args, PROC_STDERR_NULL, NULL);
    }

exit:
    if ((err != GS_OK) && (created != 0)) {
        (void)sys_remove(str_cstr(&file));
    }
    str_free(&key);
    str_free(&file);
    return err;
}

eGsErr_t cmd_drop_gitshared(const Repo_t *r, const char *name)
{
    const char *args[] = { "config", "-f", NULL, "--remove-section", NULL,
                           NULL };
    Repo_t left = REPO_INIT;
    Str_t file = STR_INIT;
    Str_t section = STR_INIT;
    eGsErr_t err;

    str_addf(&file, "%s/.gitshared", str_cstr(&r->top));
    str_addf(&section, "shared.%s", name);
    args[2] = str_cstr(&file);
    args[4] = str_cstr(&section);
    err = git_run(args, 0U, NULL);
    if (err == GS_OK) {
        err = repo_load_gitshared(&left, str_cstr(&file), 0, 0);
    }
    if ((err == GS_OK) && (left.n == 0U) && (sys_remove(str_cstr(&file)) != 0)) {
        err = GS_ERR_SYS;
    }
    if (err == GS_OK) {
        err = cmd_stage(r, ".gitshared");
    }
    repo_close(&left);
    str_free(&section);
    str_free(&file);
    return err;
}

/*
 * Un solo processo (ls-files), con i pathspec letterali: "*.h" non e' un
 * glob. Con -z il path arriva cosi' com'e': senza, git mette tra virgolette
 * e in ottale i path con caratteri non ASCII (core.quotePath, reportIdea
 * C-01).
 */
eGsErr_t cmd_resolve_tracked(const char *dir, const char *path, Str_t *full)
{
    const char *args[] = { "-C", ".", "--literal-pathspecs", "ls-files", "-s",
                           "-z", "--full-name", "--", NULL, NULL };
    Str_t out = STR_INIT;
    Str_t on_disk = STR_INIT;
    const char *line;
    const char *tab;
    size_t rec_len;
    eGsErr_t err = GS_ERR_USAGE;

    if (strchr(path, '\n') != NULL) {
        msg_error(MSG_ERR_PATH_NEWLINE);
        goto exit;
    }
    if (dir != NULL) {
        args[1] = dir;
        str_addf(&on_disk, "%s/%s", dir, path);
    } else {
        str_add(&on_disk, path);
    }
    if (sys_is_dir(str_cstr(&on_disk))) {
        msg_error(MSG_ERR_IS_DIR, path);
        goto exit;
    }
    args[8] = path;
    if (git_run(args, 0U, &out) != GS_OK) {
        msg_fatal(MSG_ERR_PROC, "git ls-files");
        err = GS_ERR_GIT;
        goto exit;
    }
    /* Record: "<modo> <oid> <stage>\t<path>\0"; piu' record = stage di
     * conflitto. str_cstr vede solo il primo record. */
    line = str_cstr(&out);
    rec_len = strlen(line);
    tab = strchr(line, '\t');
    if ((out.len == 0U) || (tab == NULL)) {
        msg_error(MSG_ERR_NOT_TRACKED, path);
    } else if (((rec_len + 1U) < out.len) || (tab[-1] != '0')) {
        msg_error(MSG_ERR_CONFLICT, path);
    } else if ((strncmp(line, "100644 ", 7U) != 0) &&
               (strncmp(line, "100755 ", 7U) != 0)) {
        msg_error(MSG_ERR_NOT_REGULAR, path);
    } else {
        str_add(full, tab + 1);
        err = GS_OK;
    }

exit:
    str_free(&on_disk);
    str_free(&out);
    return err;
}

int cmd_path_exists(const Repo_t *r, GitBatch_t *b, const char *path)
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

eGsErr_t cmd_stage(const Repo_t *r, const char *path)
{
    const char *args[] = { "-C", NULL, "update-index", "--add", "--remove",
                           "--", path, NULL };

    args[1] = str_cstr(&r->top);
    return git_run(args, 0U, NULL);
}

eGsErr_t cmd_write_file(const Repo_t *r, const char *path, const Oid_t *blob)
{
    const char *a_index[] = { "-C", NULL, "update-index", "--add",
                              "--cacheinfo", NULL, NULL };
    const char *a_out[] = { "-C", NULL, "checkout-index", "-f", "--", path,
                            NULL };
    char hex[OID_HEX_SIZE];
    Str_t info = STR_INIT;
    eGsErr_t err;

    /* Modo sempre 100644 nella v0 (bit di esecuzione: A-15). */
    oid_to_hex(blob, hex);
    str_addf(&info, "100644,%s,%s", hex, path);
    a_index[1] = str_cstr(&r->top);
    a_index[5] = str_cstr(&info);
    err = git_run(a_index, 0U, NULL);
    if (err == GS_OK) {
        a_out[1] = str_cstr(&r->top);
        err = git_run(a_out, 0U, NULL);
    }
    str_free(&info);
    return err;
}

int cmd_worktree_differs(const Repo_t *r, const char *path)
{
    /* "git diff" senza revisioni confronta l'index con il working tree,
     * normalizzando i fine riga come fa git: 0 = uguali, 1 = diversi.
     * E' porcelain: con diff.autoRefreshIndex=false un file solo toccato
     * (stesso contenuto, data diversa) risulterebbe diverso, quindi la si
     * fissa a true (reportIdea B-32, D-094). diff.external e textconv non
     * contano con --quiet (provato). */
    const char *args[] = { "-C", NULL, "-c", "diff.autoRefreshIndex=true",
                           "--literal-pathspecs", "diff", "--quiet", "--",
                           path, NULL };
    Str_t out = STR_INIT;
    int code = 1;

    args[1] = str_cstr(&r->top);
    if (git_capture(args, PROC_STDERR_NULL, &out, &code) != GS_OK) {
        code = 1;
    }
    str_free(&out);
    return code != 0;
}

int cmd_has_local_changes(const Repo_t *r, const SharedEntry_t *e, GitBatch_t *b)
{
    const char *path = str_cstr(&e->path);
    OidList_t chain = OIDLIST_INIT;
    Str_t name = STR_INIT;
    Oid_t h;
    Oid_t i;
    int local = 1; /* nel dubbio, il lavoro dell'utente si protegge */

    /* 1. Working tree diverso dall'index: modifica non in staging. */
    if (cmd_worktree_differs(r, path)) {
        goto exit;
    }
    /* 2. Index (stage 0; assente se in conflitto o tolto dall'index). */
    str_addf(&name, ":%s", path);
    if (git_batch_blob(b, str_cstr(&name), &i) == 0) {
        goto exit;
    }
    /* 3. Index uguale a HEAD: nessuna modifica. */
    str_reset(&name);
    str_addf(&name, "HEAD:%s", path);
    if ((git_batch_blob(b, str_cstr(&name), &h) != 0) && oid_eq(&i, &h)) {
        local = 0;
        goto exit;
    }
    /* 4. Index uguale a una versione della catena: e' un update applicato
     * e non committato, non lavoro dell'utente (D-076). */
    if ((chain_blobs(b, e->id, &chain) == GS_OK) && oidlist_has(&chain, &i)) {
        local = 0;
    }

exit:
    oidlist_free(&chain);
    str_free(&name);
    return local;
}

eGsErr_t cmd_commit(const Repo_t *r, const char *message, const Str_t *paths)
{
    size_t n = str_zcount(paths);
    const char **args = xmalloc((n + 10U) * sizeof *args);
    const char *p;
    size_t i = 0U;
    eGsErr_t err;

    /* --only: solo questi path, anche se in staging c'e' altro (B-25).
     * --literal-pathspecs: un nome con '*' o '?' non diventa un glob che
     * fa entrare altri file nel commit (C-06). */
    args[i++] = "-C";
    args[i++] = str_cstr(&r->top);
    args[i++] = "--literal-pathspecs";
    args[i++] = "commit";
    args[i++] = "-q";
    args[i++] = "-m";
    args[i++] = message;
    args[i++] = "--only";
    args[i++] = "--";
    for (p = str_znext(paths, NULL); p != NULL; p = str_znext(paths, p)) {
        args[i++] = p;
    }
    args[i] = NULL;
    err = git_run(args, 0U, NULL);
    free((void *)args);
    return err;
}

int cmd_reopen(Repo_t *r, GitBatch_t *b)
{
    /* Da capo: il post-commit puo' aver gia' completato delle attese
     * durante il commit (D-100), e la repo letta prima non vale piu'. */
    git_batch_close(b);
    repo_close(r);
    return cmd_open(r, b);
}

/* Nuova versione: blob di HEAD, genitore T, creatore e sovrani di T
 * (D-103, D-111), ref condizionata a T. H e T sono quelli da cui e' stato
 * ricavato lo stato (reportIdea B-31). */
static eGsErr_t new_version(const SharedEntry_t *e, GitBatch_t *b,
                            const StateInfo_t *si, Oid_t *v_out)
{
    Str_t ref = STR_INIT;
    AuthBlobs_t auth;
    Oid_t v;
    eGsErr_t err;

    auth_blobs(b, &si->tip, &auth);
    err = chain_commit(&si->h, &si->tip, NULL, &auth, str_cstr(&e->name), &v);

    if (err == GS_OK) {
        /* Condizionata: se nel frattempo un peer ha cambiato T, non si
         * sovrascrive niente (REQ-NF-005, D-072). */
        chain_ref(&ref, e->id);
        err = git_update_ref(str_cstr(&ref), &v, &si->tip, "git shared: publish");
    }
    if (err == GS_OK) {
        *v_out = v;
    }
    str_free(&ref);
    return err;
}

/*
 * Peer a cui manca la versione t (file allineato): chi ha gia' t come
 * ultima versione, o l'avviso di t (ricevera' da solo, D-099), non va
 * avvisato di nuovo. In todo gli altri, compresi quelli irraggiungibili
 * (l'avviso lo dice). Ritorna quanti hanno gia' t.
 */
static unsigned peers_missing(const SharedEntry_t *e, const Oid_t *t,
                              Str_t *todo)
{
    size_t n = str_zcount(&e->peers);
    Oid_t *tips = xmalloc(((n > 0U) ? n : 1U) * sizeof *tips);
    const char *p;
    size_t k;
    unsigned have = 0U;

    peers_tips(e->id, &e->peers, tips);
    for (k = 0U, p = str_znext(&e->peers, NULL); p != NULL;
         k++, p = str_znext(&e->peers, p)) {
        if (((tips[k].len > 0U) && oid_eq(&tips[k], t)) ||
            inbox_has(p, e->id, t)) {
            have++;
        } else {
            str_addz(todo, p);
        }
    }
    free(tips);
    return have;
}

int cmd_publish_entry(const Repo_t *r, const SharedEntry_t *e, GitBatch_t *b,
                      const StateInfo_t *create, Oid_t *tip)
{
    PeerTally_t tally = { 0U, 0U, 0U };
    Str_t push = STR_INIT; /* peer con .git file: ripiego sul push */
    Str_t todo = STR_INIT; /* peer da avvisare */
    const char *label = str_cstr(&e->path);
    const char *p;
    unsigned npeers = (unsigned)str_zcount(&e->peers);
    unsigned have = 0U; /* peer che hanno gia' T */
    Oid_t t = *tip;
    AuthBlobs_t auth;
    int rc = GS_EXIT_OK;

    if (create != NULL) {
        if (new_version(e, b, create, &t) != GS_OK) {
            msg_error(MSG_ERR_PUBLISH, label);
            rc = GS_EXIT_ERROR;
            goto exit;
        }
        *tip = t;
        printf(msg(MSG_PUB_CREATED), label);
        putchar('\n');
    }
    if (npeers == 0U) {
        /* Con un creatore le versioni vanno in remoto: niente da dire qui
         * (D-104). */
        auth_blobs(b, &t, &auth);
        if (auth.creator.len == 0U) {
            printf(msg(MSG_PUB_NO_PEERS), label);
            putchar('\n');
        }
        goto exit;
    }
    if (create == NULL) {
        /* Allineato: si avvisa solo chi non ha ancora T. Se ce l'hanno
         * tutti non c'e' niente da fare. */
        have = peers_missing(e, &t, &todo);
        if (have == npeers) {
            printf(msg(MSG_PUB_UP_TO_DATE), label);
            putchar('\n');
            goto exit;
        }
    } else {
        for (p = str_znext(&e->peers, NULL); p != NULL;
             p = str_znext(&e->peers, p)) {
            str_addz(&todo, p);
        }
    }
    /* In locale una notifica, non la catena (D-099): nessun processo. */
    inbox_notify(label, e->id, &todo, str_cstr(&r->top), &t, &tally, &push);
    if (push.len > 0U) {
        peers_push(label, e->id, &push, &tally);
    }
    if (have == 0U) {
        printf(msg(MSG_PUB_NOTIFIED), label, tally.ok, npeers);
    } else {
        printf(msg(MSG_PUB_NOTIFIED_SOME), label, tally.ok, npeers - have, have);
    }
    putchar('\n');
    if (tally.rejected > 0U) {
        rc = GS_EXIT_ERROR;
    }

exit:
    str_free(&todo);
    str_free(&push);
    return rc;
}

void cmd_auth_how(const SharedEntry_t *e, const Received_t *rv, size_t k, Str_t *how)
{
    str_reset(how);
    if (receive_pending_changes(rv) > 1U) {
        str_addf(how, "%s ", str_cstr(&rv->changes[k].authority));
    }
    str_add(how, str_cstr(&e->path));
}

/* "<oid breve>, <autore>, <data>" di una versione: chi l'ha creata
 * (D-111; chi l'ha spinta git non lo registra). */
static void version_info(const Oid_t *v, Str_t *out)
{
    const char *args[] = { "log", "-1", "--format=%h, %an, %as", NULL, NULL };
    char hex[OID_HEX_SIZE];

    oid_to_hex(v, hex);
    args[3] = hex;
    str_reset(out);
    if ((git_run(args, 0U, out) != GS_OK) || (out->len == 0U)) {
        str_reset(out);
        str_add(out, hex);
    }
}

void cmd_auth_describe(const AuthChange_t *c, Str_t *from, Str_t *info)
{
    version_info(&c->version, info);
    str_reset(from);
    if (c->by_authority != 0U) {
        str_add(from, msg(MSG_AUTH_BY_AUTHORITY));
    } else if (c->from.len > 0U) {
        str_addf(from, msg(MSG_AUTH_BY_PEER), str_cstr(&c->from));
    } else {
        str_add(from, msg(MSG_AUTH_BY_UNKNOWN));
    }
}

void cmd_print_auth_changes(const SharedEntry_t *e, const Received_t *rv, int warn)
{
    const AuthChange_t *c;
    const char *tab;
    Str_t how = STR_INIT;
    Str_t info = STR_INIT;
    Str_t from = STR_INIT;
    size_t k;

    for (k = 0U; k < rv->nchanges; k++) {
        c = &rv->changes[k];
        if (c->rejected != 0U) {
            continue; /* gia' deciso */
        }
        cmd_auth_how(e, rv, k, &how);
        if (warn != 0) {
            msg_warning(MSG_WARN_AUTH_PENDING, str_cstr(&e->path),
                        str_cstr(&c->authority), str_cstr(&how));
            continue;
        }
        cmd_auth_describe(c, &from, &info);
        printf("    %-6s ", msg(MSG_STATUS_FIELD_AUTHORITY));
        printf(msg(MSG_AUTH_CHANGE), str_cstr(&c->authority), str_cstr(&from),
               str_cstr(&info));
        putchar('\n');
        tab = strchr(str_cstr(&c->key), '\t');
        if ((c->creator != 0U) && (tab != NULL)) {
            printf("    %-9s ", "");
            printf(msg(MSG_AUTH_NEW_CREATOR), tab + 1);
            putchar('\n');
        }
        printf("    %-9s ", "");
        printf(msg(MSG_AUTH_HOW), str_cstr(&how));
        putchar('\n');
    }
    str_free(&from);
    str_free(&info);
    str_free(&how);
}

void cmd_warn_suspended(const SharedEntry_t *e, BranchNames_t *bn)
{
    Str_t list = STR_INIT;

    msg_warning(MSG_WARN_SUSPENDED, str_cstr(&e->path));
    if (shared_branches_gone(bn, e, &list)) {
        msg_warning(MSG_WARN_BRANCHES_GONE, str_cstr(&e->path), str_cstr(&list),
                    str_cstr(&e->path));
    }
    str_free(&list);
}

int cmd_repo_path(const Repo_t *r, const char *user_path, Str_t *out)
{
    return !path_is_absolute(user_path) &&
           path_join_norm(str_cstr(&r->prefix), user_path, out);
}

int cmd_select(Repo_t *r, const char *user_path, unsigned char *sel)
{
    Str_t full = STR_INIT;
    const SharedEntry_t *e;
    size_t i;
    int ok = 1;

    if ((user_path != NULL) && !cmd_repo_path(r, user_path, &full)) {
        str_reset(&full); /* nessuna voce avra' questo path */
    }
    for (i = 0U; i < r->n; i++) {
        e = &r->v[i];
        sel[i] = (e->in_gitshared && e->valid && e->in_config &&
                  (e->pending == SHARED_PENDING_NONE) &&
                  ((user_path == NULL) || str_eq(&e->path, str_cstr(&full))))
                     ? 1U
                     : 0U;
    }
    /* Un file che non si puo' scegliere lo dice, con il motivo
     * (reportIdea B-37): errore se indicato, una riga se no. */
    e = (user_path != NULL) ? repo_find_path(r, str_cstr(&full)) : NULL;
    if ((user_path != NULL) && ((e == NULL) || !e->valid)) {
        msg_error(MSG_ERR_NOT_SHARED_PATH, user_path);
        ok = 0;
    } else if ((e != NULL) && (e->pending != SHARED_PENDING_NONE)) {
        msg_error(MSG_ERR_PENDING_OP, str_cstr(&e->path));
        ok = 0;
    } else if ((e != NULL) && !e->in_config) {
        msg_error(MSG_ERR_NOT_LINKED, str_cstr(&e->path));
        ok = 0;
    } else if (user_path == NULL) {
        for (i = 0U; i < r->n; i++) {
            e = &r->v[i];
            if (e->in_gitshared && e->valid && (sel[i] == 0U)) {
                printf(msg(MSG_SKIP_STATE), str_cstr(&e->path),
                       state_name((e->pending != SHARED_PENDING_NONE)
                                      ? STATE_PENDING
                                      : STATE_UNLINKED));
                putchar('\n');
            }
        }
    } else {
        /* scelto */
    }
    str_free(&full);
    return ok;
}

void cmd_fetch_creators(Repo_t *r, GitBatch_t *b, const unsigned char *sel,
                        unsigned char *res, Str_t *urls, unsigned char *sov)
{
    size_t n = r->n;
    uint8_t *got = xmalloc((n > 0U) ? n : 1U);
    size_t *idx = xmalloc(((n > 0U) ? n : 1U) * sizeof *idx);
    Str_t ref = STR_INIT;
    Str_t ids = STR_INIT;
    Oid_t t;
    size_t i;
    size_t j;
    size_t k;
    size_t m;
    int sovereign;

    for (i = 0U; i < n; i++) {
        res[i] = CMD_NO_CREATOR;
        str_reset(&urls[i]);
        sovereign = 0;
        if (sel[i] != 0U) {
            /* Catena assente: prima si ricostruisce, senza rete (B-51). */
            chain_ref(&ref, r->v[i].id);
            if (((git_batch_query(b, str_cstr(&ref), &t) != GIT_OBJ_COMMIT) &&
                 (receive_restore_chain(&r->v[i], b, &t) != GS_OK)) ||
                (auth_target(b, &t, &urls[i], &sovereign) != GS_OK)) {
                str_reset(&urls[i]);
                sovereign = 0;
            }
        }
        if (sov != NULL) {
            sov[i] = (unsigned char)sovereign;
        }
    }
    /* Un fetch per autorita': le voci con lo stesso URL, anche scritto in
     * un'altra forma (D-112, P-04), vanno insieme. */
    for (i = 0U; i < n; i++) {
        if ((sel[i] == 0U) || (urls[i].len == 0U) || (res[i] != CMD_NO_CREATOR)) {
            continue;
        }
        str_reset(&ids);
        m = 0U;
        for (j = i; j < n; j++) {
            if ((sel[j] != 0U) && (res[j] == CMD_NO_CREATOR) && (urls[j].len > 0U) &&
                remote_same(str_cstr(&urls[j]), str_cstr(&urls[i]))) {
                str_addz(&ids, r->v[j].id);
                idx[m++] = j;
            }
        }
        remote_fetch_seen(str_cstr(&urls[i]), &ids, got);
        for (k = 0U; k < m; k++) {
            res[idx[k]] = got[k];
            /* Vista adesso su questo URL (D-112): la data per status e il
             * confronto valgono solo per l'URL su cui e' stata vista. */
            if (got[k] == (uint8_t)REMOTE_GOT) {
                (void)remote_seen_write(r->v[idx[k]].id, str_cstr(&urls[i]),
                                        &r->v[idx[k]].seen_url,
                                        &r->v[idx[k]].seen_at);
            }
        }
    }
    str_free(&ids);
    str_free(&ref);
    free(idx);
    free(got);
}
