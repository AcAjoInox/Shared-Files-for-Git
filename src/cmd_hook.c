/*
 * cmd_hook.c - "git shared hook install" e "git shared hook <evento>".
 *
 * Gli eventi li chiama il blocco installato negli hook (hooks.c), non
 * l'utente. Escono sempre con 0: un hook di git shared non blocca mai git;
 * i problemi diventano avvisi. Gli hook sono solo comodita' (D-041).
 *
 *   post-commit    (solo con shared.autoPublish, D-073) completa set/link
 *                  in attesa, pubblica, avvisa se il commit ha reso un file
 *                  divergente
 *   post-checkout, post-merge, post-rewrite
 *                  avvisano se un file non e' allineato (REQ-F-013)
 *
 * Percorso rapido (D-069): git lancia gli hook dalla radice del working
 * tree; senza ./.gitshared si esce senza lanciare processi. Il post-commit
 * lancia un solo processo (i path toccati dal commit) ed esce se nessuno
 * compare nemmeno come testo in .gitshared. E' solo un filtro: se passa,
 * .gitshared si legge con git config come sempre.
 */
#include <stdio.h>
#include <string.h>

#include "cmd.h"
#include "cmd_common.h"
#include "hooks.h"
#include "state.h"
#include "sys.h"

/* Path toccati dal commit e voci di .gitshared. */
typedef struct {
    Str_t touched;   /* path toccati, elenco compatto */
    Repo_t declared; /* voci di ./.gitshared */
    unsigned any_shared : 1U;
    unsigned gitshared : 1U;
} Touched_t;

static void touched_free(Touched_t *t)
{
    str_free(&t->touched);
    repo_close(&t->declared);
}

/* Testo di ./.gitshared, per il filtro del percorso rapido. */
static void read_gitshared_text(Str_t *text)
{
    char chunk[1024];
    size_t n;
    FILE *f = sys_fopen(".gitshared", "rb");

    if (f != NULL) {
        while ((n = fread(chunk, 1U, sizeof chunk, f)) > 0U) {
            str_addn(text, chunk, n);
        }
        (void)fclose(f);
    }
}

/* Path toccati dall'ultimo commit (HEAD rispetto al primo genitore). */
static eGsErr_t find_touched(Touched_t *t)
{
    static const char *const args[] = { "diff-tree", "--no-commit-id",
                                        "--name-only", "-r", "-z", "--root",
                                        "--no-renames", "HEAD", NULL };
    Str_t out = STR_INIT;
    Str_t text = STR_INIT;
    const char *c;
    const char *end;
    size_t i;
    int code = 0;
    int maybe = 0;
    eGsErr_t err;

    err = git_capture(args, PROC_STDERR_NULL, &out, &code);
    if ((err != GS_OK) || (code != 0)) {
        err = GS_ERR_GIT;
        goto exit;
    }
    /* -z: i path sono separati da '\0', gia' nel formato di str_z* */
    c = str_cstr(&out);
    end = c + out.len;
    while (c < end) {
        str_addz(&t->touched, c);
        c += strlen(c) + 1U;
    }
    t->gitshared = str_zhas(&t->touched, ".gitshared") ? 1U : 0U;

    /* Filtro: un path shared compare per forza nel testo di .gitshared. */
    read_gitshared_text(&text);
    for (c = str_znext(&t->touched, NULL); (c != NULL) && (maybe == 0);
         c = str_znext(&t->touched, c)) {
        maybe = (strstr(str_cstr(&text), c) != NULL);
    }
    if (maybe == 0) {
        goto exit; /* nessun file shared toccato: niente git config */
    }
    (void)repo_load_gitshared(&t->declared, ".gitshared", 0, 0);
    for (i = 0U; i < t->declared.n; i++) {
        if (t->declared.v[i].valid &&
            str_zhas(&t->touched, str_cstr(&t->declared.v[i].path))) {
            t->any_shared = 1U;
        }
    }

exit:
    str_free(&text);
    str_free(&out);
    return err;
}

/* Voce da controllare: collegata, non in attesa, sul suo branch attivo. */
static int is_live(const Repo_t *r, const SharedEntry_t *e)
{
    return e->in_gitshared && e->valid && e->in_config &&
           (e->pending == SHARED_PENDING_NONE) &&
           shared_is_active(e, str_cstr(&r->branch));
}

static void hook_post_commit(void)
{
    Touched_t t = { STR_INIT, REPO_INIT, 0U, 0U };
    Repo_t repo = REPO_INIT;
    GitBatch_t batch;
    const SharedEntry_t *e;
    eState_t st;
    size_t i;

    if ((find_touched(&t) != GS_OK) ||
        ((t.any_shared == 0U) && (t.gitshared == 0U))) {
        goto exit;
    }
    if (repo_open(&repo) != GS_OK) {
        goto exit;
    }
    if (git_batch_open(&batch) == GS_OK) {
        /* Il commit puo' confermare un set/link (REQ-F-023). */
        if ((t.gitshared != 0U) && (repo_complete_pending(&repo, &batch) != GS_OK)) {
            msg_error(MSG_ERR_PENDING);
        }
        /* Stato ricavato ora, con la T di adesso (D-070, D-072): se un peer
         * ha pubblicato nel frattempo il file risulta divergente e non si
         * pubblica. L'hook c'e' solo con autoPublish, ma lo si ricontrolla:
         * la configurazione puo' essere cambiata dopo l'installazione. */
        for (i = 0U; i < repo.n; i++) {
            e = &repo.v[i];
            if (is_live(&repo, e) && str_zhas(&t.touched, str_cstr(&e->path)) &&
                (state_derive(&repo, e, &batch, &st) == GS_OK)) {
                if ((st == STATE_TO_PUBLISH) && (repo.auto_publish != 0U)) {
                    (void)cmd_publish_entry(e, &batch, 1);
                } else if (st == STATE_DIVERGED) {
                    msg_warning(MSG_HOOK_DIVERGED, str_cstr(&e->path));
                } else {
                    /* allineato (commit di un update) o da pubblicare a mano */
                }
            }
        }
        git_batch_close(&batch);
    }
    repo_close(&repo);

exit:
    touched_free(&t);
}

/* post-checkout, post-merge, post-rewrite: avviso, senza modificare nulla
 * (D-010, REQ-F-013). Nessun avviso per i file sospesi (REQ-F-022). */
static void hook_warn(void)
{
    Repo_t repo = REPO_INIT;
    GitBatch_t batch;
    const SharedEntry_t *e;
    const char *command;
    eState_t st;
    size_t i;

    if (repo_open(&repo) != GS_OK) {
        goto exit;
    }
    if (git_batch_open(&batch) == GS_OK) {
        for (i = 0U; i < repo.n; i++) {
            e = &repo.v[i];
            if (is_live(&repo, e) && (state_derive(&repo, e, &batch, &st) == GS_OK) &&
                (st != STATE_ALIGNED) && state_is_derived(st)) {
                command = (st == STATE_TO_PUBLISH) ? "publish" : "update";
                msg_warning(MSG_HOOK_NOT_ALIGNED, str_cstr(&e->path),
                            state_name(st), command);
            }
        }
        git_batch_close(&batch);
    }

exit:
    repo_close(&repo);
}

/* "git shared hook install": allinea gli hook alla configurazione (es.
 * dopo aver cambiato shared.autoPublish, o in una repo con file shared
 * dichiarati prima degli hook). Lanciato dall'utente: il codice di uscita
 * conta. */
static int hook_install(void)
{
    Repo_t repo = REPO_INIT;
    eGsErr_t err = repo_open(&repo);
    int rc = GS_EXIT_FATAL;

    if (err != GS_OK) {
        msg_fatal((err == GS_ERR_NOT_REPO) ? MSG_ERR_NOT_REPO : MSG_ERR_PROC, "git");
    } else if (hooks_sync(&repo) == GS_OK) {
        printf(msg(MSG_HOOKS_INSTALLED), (repo.auto_publish != 0U) ? "on" : "off");
        putchar('\n');
        rc = GS_EXIT_OK;
    } else {
        rc = GS_EXIT_ERROR; /* avvisi gia' stampati */
    }
    repo_close(&repo);
    return rc;
}

int cmd_hook(int argc, char **argv)
{
    FILE *f;
    const char *event = (argc > 1) ? argv[1] : "";
    int rc = GS_EXIT_OK;

    if (strcmp(event, "install") == 0) {
        rc = hook_install();
        goto exit;
    }
    /* Repo senza file shared: nessun processo. */
    f = sys_fopen(".gitshared", "rb");
    if (f == NULL) {
        goto exit;
    }
    (void)fclose(f);
    if (strcmp(event, "post-commit") == 0) {
        hook_post_commit();
    } else if (strcmp(event, "post-checkout") == 0) {
        /* terzo argomento 0 = checkout di file, non di branch */
        if ((argc < 5) || (strcmp(argv[4], "0") != 0)) {
            hook_warn();
        }
    } else if ((strcmp(event, "post-merge") == 0) ||
               (strcmp(event, "post-rewrite") == 0)) {
        hook_warn();
    } else {
        /* altri eventi (es. pre-commit di M3 non ancora ripulito): niente */
    }

exit:
    return rc;
}
