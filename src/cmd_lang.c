/*
 * cmd_lang.c - "git shared lang [en | it]" (REQ-NF-008, D-088).
 *
 * Scrive shared.lang nella configurazione globale: vale per tutti i
 * repository dell'utente. Per ora sceglie la lingua delle pagine di
 * aiuto (cmd_help.c); i messaggi dei comandi restano in inglese.
 */
#include <stdio.h>
#include <string.h>

#include "cmd.h"
#include "err.h"
#include "git.h"
#include "help_text.h"
#include "msg.h"

int cmd_lang(int argc, char **argv)
{
    const char *args[] = { "config", "--global", "shared.lang", NULL, NULL };
    int rc = GS_EXIT_USAGE;

    if (argc > 2) {
        msg_error(MSG_ERR_TOO_MANY_ARGS);
        fputs(msg(MSG_USAGE_LANG), stderr);
        goto exit;
    }
    if (argc < 2) {
        printf(msg(MSG_LANG_CURRENT), help_lang());
        putchar('\n');
        rc = GS_EXIT_OK;
        goto exit;
    }
    if ((strcmp(argv[1], "en") != 0) && (strcmp(argv[1], "it") != 0)) {
        msg_error(MSG_ERR_LANG, argv[1]);
        fputs(msg(MSG_USAGE_LANG), stderr);
        goto exit;
    }
    args[3] = argv[1];
    if (git_run(args, 0U, NULL) != GS_OK) {
        msg_error(MSG_ERR_LANG_WRITE);
        rc = GS_EXIT_ERROR;
        goto exit;
    }
    printf(msg(MSG_LANG_SET), argv[1]);
    putchar('\n');
    rc = GS_EXIT_OK;

exit:
    return rc;
}
