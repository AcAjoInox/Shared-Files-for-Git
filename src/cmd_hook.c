/*
 * cmd_hook.c - "git shared hook install" e "git shared hook <evento>".
 *
 * Gli eventi li chiama il blocco installato negli hook (hooks.c), non
 * l'utente. Escono sempre con 0: un hook di git shared non blocca mai git;
 * i problemi diventano avvisi. Gli hook sono solo comodita' (D-041).
 *
 *   post-checkout, post-merge, post-rewrite
 *                  completano le operazioni in attesa, se ce ne sono, e
 *                  avvisano se un file non e' allineato (REQ-F-013) o se
 *                  un peer ha annunciato una versione nuova (D-099)
 *   post-commit    completa set, link e unlink in attesa (D-100). Il blocco
 *                  c'e' solo finche' un'attesa e' aperta: chiusa l'ultima,
 *                  si toglie. Non pubblica mai (D-074).
 *   complete       interno, lanciato da link nella sorgente con
 *                  "git -C <sorgente> shared hook complete": completa
 *                  soltanto, non scarta nulla (D-100)
 *
 * Il blocco nel pre-commit di una versione precedente chiama ancora
 * "git-shared hook pre-commit": non fa nulla, e "hook install" lo toglie.
 *
 * Percorso rapido (D-069): git lancia gli hook dalla radice del working
 * tree; senza ./.gitshared si esce senza lanciare processi. Non per il
 * post-commit: un unlink dell'ultimo file toglie anche .gitshared, e il
 * blocco c'e' solo con un'attesa aperta.
 */
#include <stdio.h>
#include <string.h>

#include "chain.h"
#include "cmd.h"
#include "cmd_common.h"
#include "hooks.h"
#include "inbox.h"
#include "state.h"
#include "sys.h"

/* Voce da controllare: collegata, non in attesa, sul suo branch attivo. */
static int is_live(const Repo_t *r, const SharedEntry_t *e)
{
    return e->in_gitshared && e->valid && e->in_config &&
           (e->pending == SHARED_PENDING_NONE) &&
           shared_is_active(e, str_cstr(&r->branch));
}

/* 1 se un peer ha annunciato una versione diversa da T (D-099). Niente
 * fetch negli hook: girano dentro le operazioni di git. */
static int announced(const Repo_t *r, const SharedEntry_t *e, GitBatch_t *b)
{
    Inbox_t in = INBOX_INIT;
    Str_t ref = STR_INIT;
    Oid_t t;
    size_t k;
    int found = 0;

    inbox_read(r, e->id, &in);
    if (in.oids.n > 0U) {
        chain_ref(&ref, e->id);
        if (git_batch_query(b, str_cstr(&ref), &t) != GIT_OBJ_COMMIT) {
            t.len = 0U;
        }
        for (k = 0U; (k < in.oids.n) && (found == 0); k++) {
            found = !oid_eq(&in.oids.v[k], &t);
        }
    }
    inbox_free(&in);
    str_free(&ref);
    return found;
}

/* post-checkout, post-merge, post-rewrite: avviso, senza modificare nulla
 * (D-010, REQ-F-013). Nessun avviso per i file sospesi (REQ-F-022). */
static void hook_warn(void)
{
    Repo_t repo = REPO_INIT;
    GitBatch_t batch;
    BranchNames_t names = BRANCHNAMES_INIT;
    Received_t rv = RECEIVED_INIT;
    Str_t list = STR_INIT;
    const SharedEntry_t *e;
    const char *command;
    eState_t st;
    size_t i;

    if (repo_open(&repo) != GS_OK) {
        goto exit;
    }
    if (git_batch_open(&batch) == GS_OK) {
        /* .gitshared arrivato con un merge, un rebase o un checkout */
        if (repo_has_pending(&repo)) {
            (void)cmd_complete(&repo, &batch, 1);
            repo_check_self(&repo);
        }
        for (i = 0U; i < repo.n; i++) {
            e = &repo.v[i];
            /* Sospeso perche' nessun suo branch esiste piu' (B-21): l'unico
             * avviso per un file sospeso. */
            if (e->in_gitshared && e->valid && e->in_config &&
                (e->pending == SHARED_PENDING_NONE) && !is_live(&repo, e) &&
                shared_branches_gone(&names, e, &list)) {
                msg_warning(MSG_WARN_BRANCHES_GONE, str_cstr(&e->path),
                            str_cstr(&list), str_cstr(&e->path));
            }
            if (is_live(&repo, e) && (state_derive(&repo, e, &batch, &st, NULL) == GS_OK) &&
                (st != STATE_ALIGNED) && state_is_derived(st)) {
                if (st == STATE_TO_COMMIT) {
                    msg_warning(MSG_UPD_TO_COMMIT, str_cstr(&e->path));
                } else {
                    command = (st == STATE_TO_PUBLISH) ? "publish" : "update";
                    msg_warning(MSG_HOOK_NOT_ALIGNED, str_cstr(&e->path),
                                state_name(st), command);
                }
            }
            if (is_live(&repo, e) && announced(&repo, e, &batch)) {
                msg_warning(MSG_HOOK_ANNOUNCED, str_cstr(&e->path));
            }
            /* Cambio di autorita' da decidere (D-111): una nota, senza
             * rete ne' domande. */
            if (is_live(&repo, e) &&
                (receive_pending_auth(&repo, e, &batch, &rv) == GS_OK)) {
                cmd_print_auth_changes(e, &rv, 1);
            }
        }
        git_batch_close(&batch);
    }

exit:
    repo_close(&repo);
    branchnames_free(&names);
    received_free(&rv);
    str_free(&list);
}

/*
 * post-commit (may_discard = 1) e complete (may_discard = 0): completa le
 * attese (D-100). Il blocco del post-commit lo toglie cmd_complete quando
 * si chiude l'ultima; uno rimasto senza attese (es. di una versione
 * precedente, shared.autoPublish) si toglie qui.
 */
static void hook_complete(int may_discard)
{
    Repo_t repo = REPO_INIT;
    GitBatch_t batch;

    if (repo_open(&repo) != GS_OK) {
        goto exit;
    }
    if (!repo_has_pending(&repo)) {
        if (may_discard != 0) {
            (void)hooks_sync_post_commit(&repo);
        }
        goto exit;
    }
    if (git_batch_open(&batch) == GS_OK) {
        if (cmd_complete(&repo, &batch, may_discard) != GS_OK) {
            msg_error(MSG_ERR_PENDING);
        }
        repo_check_self(&repo); /* un set o link appena completato */
        git_batch_close(&batch);
    }

exit:
    repo_close(&repo);
}

/* "git shared hook install": allinea gli hook (es. in una repo con file
 * shared dichiarati prima degli hook, o con i blocchi di una versione
 * precedente). Lanciato dall'utente: il codice di uscita conta. */
static int hook_install(void)
{
    Repo_t repo = REPO_INIT;
    eGsErr_t err = repo_open(&repo);
    int rc = GS_EXIT_FATAL;

    if (err != GS_OK) {
        msg_fatal((err == GS_ERR_NOT_REPO) ? MSG_ERR_NOT_REPO : MSG_ERR_PROC, "git");
    } else if (hooks_sync(&repo) == GS_OK) {
        puts(msg(MSG_HOOKS_INSTALLED));
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
    if ((strcmp(event, "post-commit") == 0) || (strcmp(event, "complete") == 0)) {
        hook_complete(strcmp(event, "post-commit") == 0);
        goto exit;
    }
    /* Repo senza file shared: nessun processo. */
    f = sys_fopen(".gitshared", "rb");
    if (f == NULL) {
        goto exit;
    }
    (void)fclose(f);
    if (strcmp(event, "post-checkout") == 0) {
        /* terzo argomento 0 = checkout di file, non di branch */
        if ((argc < 5) || (strcmp(argv[4], "0") != 0)) {
            hook_warn();
        }
    } else if ((strcmp(event, "post-merge") == 0) ||
               (strcmp(event, "post-rewrite") == 0)) {
        hook_warn();
    } else {
        /* pre-commit di versioni precedenti: niente */
    }

exit:
    return rc;
}
