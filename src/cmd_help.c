/*
 * cmd_help.c - pagine di aiuto (REQ-F-019, D-087).
 *
 *   git shared help                 pagina generale (help/git-shared.txt)
 *   git shared help <command>       pagina del comando (help/<command>.txt)
 *   git shared <command> --help     la stessa, chiamata da main
 *   git-shared --help               la pagina generale
 *
 * Lingua: shared.lang (git shared lang it), pagine di help/it/; una
 * pagina che non ha la traduzione si mostra in inglese (D-088).
 *
 * "git shared --help" invece lo intercetta git, che apre la pagina HTML
 * (Windows) o man (Linux) generata dallo stesso testo e installata da
 * make install (§6.1).
 *
 * I testi sono incorporati nell'eseguibile al build (help_text.h): le
 * pagine ci sono sempre, anche se l'eseguibile viene spostato.
 */
#include <stdio.h>
#include <string.h>

#include "cmd.h"
#include "err.h"
#include "git.h"
#include "help_text.h"
#include "msg.h"

const char *help_lang(void)
{
    static const char *lang = NULL;
    const char *args[] = { "config", "--get", "shared.lang", NULL };
    Str_t out = STR_INIT;

    if (lang == NULL) {
        lang = "en";
        if ((git_run(args, PROC_STDERR_NULL, &out) == GS_OK) &&
            str_eq(&out, "it")) {
            lang = "it";
        }
        str_free(&out);
    }
    return lang;
}

/* Pagina nella lingua data; se non c'e', quella inglese. */
static const HelpPage_t *find_page(const char *lang, const char *name)
{
    const HelpPage_t *found = NULL;
    const HelpPage_t *en = NULL;
    size_t i;

    for (i = 0U; (i < help_page_count) && (found == NULL); i++) {
        if (strcmp(help_pages[i].name, name) == 0) {
            if (strcmp(help_pages[i].lang, lang) == 0) {
                found = &help_pages[i];
            } else if (strcmp(help_pages[i].lang, "en") == 0) {
                en = &help_pages[i];
            } else {
                /* altra lingua */
            }
        }
    }
    return (found != NULL) ? found : en;
}

int cmd_help(int argc, char **argv)
{
    const HelpPage_t *page;
    size_t i;
    int rc = GS_EXIT_USAGE;

    if (argc > 2) {
        msg_error(MSG_ERR_TOO_MANY_ARGS);
        fputs(msg(MSG_USAGE_HELP), stderr);
        goto exit;
    }
    page = find_page(help_lang(), (argc < 2) ? "git-shared" : argv[1]);
    if (page == NULL) {
        msg_error(MSG_ERR_UNKNOWN_TOPIC, argv[1]);
        goto exit;
    }
    for (i = 0U; page->parts[i] != NULL; i++) {
        fputs(page->parts[i], stdout);
    }
    rc = GS_EXIT_OK;

exit:
    return rc;
}
