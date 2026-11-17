/*
 * cmd_help.c - pagine di aiuto (REQ-F-019, D-087).
 *
 *   git shared help                 pagina generale (help/git-shared.txt)
 *   git shared help <command>       pagina del comando (help/<command>.txt)
 *   git shared <command> --help     la stessa, chiamata da main
 *   git-shared --help               la pagina generale
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
#include "help_text.h"
#include "msg.h"

static const HelpPage_t *find_page(const char *name)
{
    const HelpPage_t *found = NULL;
    size_t i;

    for (i = 0U; (i < help_page_count) && (found == NULL); i++) {
        if (strcmp(help_pages[i].name, name) == 0) {
            found = &help_pages[i];
        }
    }
    return found;
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
    page = find_page((argc < 2) ? "git-shared" : argv[1]);
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
