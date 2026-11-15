/*
 * cmd_set.c - "git shared set [-c | -m <msg>] [--] <path>" (REQ-F-001).
 *
 * Al comando (REQ-F-023): operazione in attesa in .git/config, sezione in
 * .gitshared, .gitshared in staging. Al commit: catena e branch attivi
 * (repo_complete_pending). Con -c/-m il commit si fa subito.
 *
 * L'attesa e' scritta per prima: se un passo successivo fallisce resta
 * un'attesa senza sezione, che il primo comando git shared scarta.
 */
#include <stdio.h>
#include <string.h>

#include "cmd.h"
#include "err.h"
#include "git.h"
#include "msg.h"
#include "shared.h"
#include "sys.h"

static const char gitshared_header[] =
    "# Managed by 'git shared': do not edit by hand.\n"
    "# This repository shares files with other repositories: "
    "run 'git shared status'.\n";

typedef struct {
    const char *path;    /* come scritto dall'utente */
    const char *message; /* messaggio di commit; NULL = standard */
    unsigned commit : 1U;
} SetOpts_t;

/* GS_ERR_USAGE se gli argomenti non vanno (messaggio gia' stampato). */
static eGsErr_t parse_args(int argc, char **argv, SetOpts_t *o)
{
    int i = 1;
    int end_opts = 0;
    eGsErr_t err = GS_ERR_USAGE;

    o->path = NULL;
    o->message = NULL;
    o->commit = 0U;
    while ((i < argc) && (end_opts == 0) && (argv[i][0] == '-')) {
        if (strcmp(argv[i], "--") == 0) {
            end_opts = 1;
        } else if ((strcmp(argv[i], "-c") == 0) ||
                   (strcmp(argv[i], "--commit") == 0)) {
            o->commit = 1U;
        } else if (strcmp(argv[i], "-m") == 0) {
            if ((i + 1) >= argc) {
                msg_error(MSG_ERR_OPT_VALUE, argv[i]);
                goto exit;
            }
            i++;
            o->message = argv[i];
            o->commit = 1U;
        } else {
            msg_error(MSG_ERR_UNKNOWN_OPT, argv[i]);
            goto exit;
        }
        i++;
    }
    if (i >= argc) {
        msg_error(MSG_ERR_MISSING_PATH);
        goto exit;
    }
    if ((i + 1) < argc) {
        msg_error(MSG_ERR_TOO_MANY_ARGS);
        goto exit;
    }
    o->path = argv[i];
    err = GS_OK;

exit:
    if (err != GS_OK) {
        fputs(msg(MSG_USAGE_SET), stderr);
    }
    return err;
}

/*
 * Path relativo alla radice del file indicato dall'utente, controllando
 * che sia tracciato, senza conflitti, e un file regolare. Un solo processo
 * (ls-files), con i pathspec letterali: "*.h" non e' un glob.
 */
static eGsErr_t resolve_path(const char *user_path, Str_t *full)
{
    const char *args[] = { "--literal-pathspecs", "ls-files", "-s",
                           "--full-name", "--", NULL, NULL };
    Str_t out = STR_INIT;
    const char *line;
    const char *tab;
    eGsErr_t err = GS_ERR_USAGE;

    if (strchr(user_path, '\n') != NULL) {
        msg_error(MSG_ERR_PATH_NEWLINE);
        goto exit;
    }
    if (sys_is_dir(user_path)) {
        msg_error(MSG_ERR_IS_DIR, user_path);
        goto exit;
    }
    args[5] = user_path;
    if (git_run(args, 0U, &out) != GS_OK) {
        msg_fatal(MSG_ERR_PROC, "git ls-files");
        err = GS_ERR_GIT;
        goto exit;
    }
    /* Riga: "<modo> <oid> <stage>\t<path>"; piu' righe = stage di conflitto */
    line = str_cstr(&out);
    tab = strchr(line, '\t');
    if ((out.len == 0U) || (tab == NULL)) {
        msg_error(MSG_ERR_NOT_TRACKED, user_path);
    } else if ((strchr(tab, '\n') != NULL) || (tab[-1] != '0')) {
        msg_error(MSG_ERR_CONFLICT, user_path);
    } else if ((strncmp(line, "100644 ", 7U) != 0) &&
               (strncmp(line, "100755 ", 7U) != 0)) {
        msg_error(MSG_ERR_NOT_REGULAR, user_path);
    } else {
        str_add(full, tab + 1);
        err = GS_OK;
    }

exit:
    str_free(&out);
    return err;
}

/* Aggiunge la sezione a .gitshared, creandolo con l'intestazione se manca.
 * In caso di errore lascia .gitshared com'era. */
static eGsErr_t write_gitshared(const Repo_t *r, const char *name,
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
        /* default dei branch attivi: il branch corrente (D-057) */
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

int cmd_set(int argc, char **argv)
{
    SetOpts_t o;
    Repo_t repo = REPO_INIT;
    GitBatch_t batch;
    SharedEntry_t *e;
    Str_t full = STR_INIT;
    Str_t key = STR_INIT;
    Str_t text = STR_INIT;
    const char *name;
    const char *slash;
    const char *args[] = { "-C", NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                           NULL, NULL };
    char id[GS_ID_HEX_LEN + 1U];
    int batch_open = 0;
    int rc = GS_EXIT_USAGE;
    eGsErr_t err;

    if (parse_args(argc, argv, &o) != GS_OK) {
        goto exit;
    }
    rc = GS_EXIT_FATAL;
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
    if (repo_complete_pending(&repo, &batch) != GS_OK) {
        msg_error(MSG_ERR_PENDING);
    }

    rc = GS_EXIT_ERROR;
    if (resolve_path(o.path, &full) != GS_OK) {
        goto exit;
    }
    e = repo_find_path(&repo, str_cstr(&full));
    if (e != NULL) {
        msg_error(MSG_ERR_ALREADY_SHARED, str_cstr(&full), str_cstr(&e->name));
        goto exit;
    }
    slash = strrchr(str_cstr(&full), '/');
    name = (slash != NULL) ? (slash + 1) : str_cstr(&full);
    if (repo_find_name(&repo, name) != NULL) {
        msg_error(MSG_ERR_NAME_USED, name);
        goto exit;
    }
    if (shared_new_id(id) != GS_OK) {
        msg_fatal(MSG_ERR_RANDOM);
        goto exit;
    }

    /* 1. Attesa (locale): per prima, vedi intestazione. */
    str_addf(&key, "shared.%s.pending", id);
    args[0] = "config";
    args[1] = str_cstr(&key);
    args[2] = "set";
    args[3] = NULL;
    if (git_run(args, 0U, NULL) != GS_OK) {
        msg_error(MSG_ERR_PENDING);
        goto exit;
    }
    /* 2. Sezione in .gitshared. */
    if (write_gitshared(&repo, name, id, str_cstr(&full)) != GS_OK) {
        msg_error(MSG_ERR_WRITE_GITSHARED);
        goto exit;
    }
    /* 3. Staging (plumbing, REQ-NF-006). */
    args[0] = "-C";
    args[1] = str_cstr(&repo.top);
    args[2] = "update-index";
    args[3] = "--add";
    args[4] = "--";
    args[5] = ".gitshared";
    args[6] = NULL;
    if (git_run(args, 0U, NULL) != GS_OK) {
        msg_error(MSG_ERR_STAGE_GITSHARED);
        goto exit;
    }

    e = repo_add(&repo);
    str_add(&e->name, name);
    str_add(&e->path, str_cstr(&full));
    memcpy(e->id, id, sizeof id);
    e->in_gitshared = 1U;
    e->in_config = 1U;
    e->valid = 1U;
    e->pending = SHARED_PENDING_SET;

    if (o.commit == 0U) {
        printf(msg(MSG_SET_STAGED), str_cstr(&full), name);
        putchar('\n');
        rc = GS_EXIT_OK;
        goto exit;
    }

    /* 4. Commit dedicato del solo .gitshared (D-007, A-29). */
    if (o.message == NULL) {
        str_addf(&text, msg(MSG_SET_COMMIT_MSG), str_cstr(&full));
        o.message = str_cstr(&text);
    }
    args[2] = "commit";
    args[3] = "-q";
    args[4] = "-m";
    args[5] = o.message;
    args[6] = "--only";
    args[7] = "--";
    args[8] = ".gitshared";
    args[9] = NULL;
    if (git_run(args, 0U, NULL) != GS_OK) {
        msg_error(MSG_ERR_COMMIT);
        goto exit;
    }
    /* Il processo cat-file e' nato prima del commit: se ne apre uno nuovo
     * per essere certi di vedere il nuovo HEAD. */
    git_batch_close(&batch);
    batch_open = 0;
    if (git_batch_open(&batch) != GS_OK) {
        msg_fatal(MSG_ERR_PROC, "git cat-file");
        goto exit;
    }
    batch_open = 1;
    if (repo_complete_pending(&repo, &batch) != GS_OK) {
        msg_error(MSG_ERR_PENDING);
        goto exit;
    }
    /* Se il file non e' ancora in HEAD resta in attesa: l'avviso e' gia'
     * stato dato da repo_complete_pending. */
    e = repo_find_path(&repo, str_cstr(&full));
    if ((e != NULL) && (e->pending == SHARED_PENDING_NONE)) {
        printf(msg(MSG_SET_DONE), str_cstr(&full), name);
        putchar('\n');
    }
    rc = GS_EXIT_OK;

exit:
    if (batch_open != 0) {
        git_batch_close(&batch);
    }
    repo_close(&repo);
    str_free(&text);
    str_free(&key);
    str_free(&full);
    return rc;
}
