/*
 * cmd_help.c - "git shared help [<command>]" (REQ-F-019).
 *
 * Manuale incorporato, sempre disponibile anche senza man page o HTML.
 * Provvisorio: in seguito sara' generato dallo stesso sorgente della man
 * page e della pagina HTML (A-06).
 */
#include <stdio.h>
#include <string.h>

#include "cmd.h"
#include "err.h"
#include "msg.h"

static const char manual[] =
    "GIT-SHARED(1)\n"
    "\n"
    "NAME\n"
    "    git-shared - keep the same file aligned across repositories\n"
    "\n"
    "SYNOPSIS\n"
    "    git shared status\n"
    "    git shared help [<command>]\n"
    "\n"
    "DESCRIPTION\n"
    "    A shared file is a regular file tracked by several repositories.\n"
    "    Each repository keeps a real copy of it; git shared tells every\n"
    "    repository when a new version exists and applies it on request.\n"
    "\n"
    "COMMANDS\n"
    "    status\n"
    "        List the shared files declared in .gitshared.\n"
    "\n"
    "    help [<command>]\n"
    "        Show this manual, or the usage of a command.\n"
    "\n"
    "FILES\n"
    "    .gitshared\n"
    "        Versioned file at the root of the repository that declares\n"
    "        the shared files. Managed by git shared: do not edit by hand.\n"
    "\n"
    "EXIT STATUS\n"
    "    0 on success, 1 on error, 128 on fatal error, 129 on wrong usage.\n";

/* Uso di un sottocomando, per "git shared help <command>". */
static const struct {
    const char *name;
    eMsgId_t usage;
} topics[] = {
    { "help", MSG_USAGE_HELP },
    { "status", MSG_USAGE_STATUS },
};

int cmd_help(int argc, char **argv)
{
    size_t i;
    int rc = GS_EXIT_USAGE;

    if (argc < 2) {
        fputs(manual, stdout);
        rc = GS_EXIT_OK;
        goto exit;
    }
    if (argc > 2) {
        msg_error(MSG_ERR_TOO_MANY_ARGS);
        fputs(msg(MSG_USAGE_HELP), stderr);
        goto exit;
    }
    for (i = 0U; i < (sizeof topics / sizeof topics[0]); i++) {
        if (strcmp(argv[1], topics[i].name) == 0) {
            fputs(msg(topics[i].usage), stdout);
            rc = GS_EXIT_OK;
            goto exit;
        }
    }
    msg_error(MSG_ERR_UNKNOWN_TOPIC, argv[1]);

exit:
    return rc;
}
