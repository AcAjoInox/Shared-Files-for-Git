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
    "    git shared set [-c | -m <message>] [--] <path>\n"
    "    git shared link [-c | -m <message>] [--] <source-file> "
    "[<destination>]\n"
    "    git shared status\n"
    "    git shared publish [<path>]\n"
    "    git shared update [-f] [-c | -m <message>] [<path>]\n"
    "    git shared hook install\n"
    "    git shared help [<command>]\n"
    "\n"
    "DESCRIPTION\n"
    "    A shared file is a regular file tracked by several repositories.\n"
    "    Each repository keeps a real copy of it; git shared tells every\n"
    "    repository when a new version exists and applies it on request.\n"
    "\n"
    "COMMANDS\n"
    "    set [-c | -m <message>] [--] <path>\n"
    "        Declare a tracked file of this repository as shared. The\n"
    "        file is added to .gitshared, which is staged: the declaration\n"
    "        takes effect when .gitshared is committed. -c commits right\n"
    "        away with a standard message, -m <message> with <message>.\n"
    "        Nothing is committed by default.\n"
    "\n"
    "    link [-c | -m <message>] [--] <source-file> [<destination>]\n"
    "        Link to this repository a file shared by another repository,\n"
    "        given by its path in that repository. The file is written\n"
    "        from its latest version and staged with .gitshared. When they\n"
    "        are committed the repositories become peers: each one learns\n"
    "        about the other and about the other peers.\n"
    "\n"
    "    publish [<path>]\n"
    "        For each shared file changed and committed on top of the\n"
    "        latest version, create a new version and send it to the\n"
    "        peers. Nothing is published until you run it (unless\n"
    "        shared.autoPublish is on). A peer that already has a newer\n"
    "        version refuses it: run update first.\n"
    "\n"
    "    update [-f] [-c | -m <message>] [<path>]\n"
    "        Get the versions from the peers and apply the latest one to\n"
    "        the working tree and the index. Local changes are never\n"
    "        overwritten, unless -f is given. Nothing is committed by\n"
    "        default.\n"
    "\n"
    "    status\n"
    "        Show each shared file with its state: aligned, new version to\n"
    "        apply, to publish, diverged, pending commit, unlinked,\n"
    "        inconsistent. On a branch where the file is not active the\n"
    "        state is shown as suspended.\n"
    "\n"
    "    hook install\n"
    "        Align the hooks with shared.autoPublish (run it after changing\n"
    "        that setting), or install them in a repository whose shared\n"
    "        files have none yet. set and link install them by themselves.\n"
    "\n"
    "    help [<command>]\n"
    "        Show this manual, or the usage of a command.\n"
    "\n"
    "HOOKS\n"
    "    set and link add a block to some hooks. Existing hooks are kept:\n"
    "    the block goes right after their first line.\n"
    "      post-checkout, post-merge, post-rewrite\n"
    "                     warn when a shared file is not aligned\n"
    "      post-commit    only with shared.autoPublish: confirms set and\n"
    "                     link, publishes the new version of the shared\n"
    "                     files changed by the commit, warns when the commit\n"
    "                     made one of them diverge\n"
    "    No hook ever blocks git. Commits do not pay for git shared unless\n"
    "    shared.autoPublish is on. The hooks are a convenience: status,\n"
    "    publish and update do the same work.\n"
    "\n"
    "CONFIGURATION\n"
    "    shared.autoPublish\n"
    "        false (default): new versions are published by hand with\n"
    "        publish, when you decide. true: a commit that changes a shared\n"
    "        file on one of its active branches publishes it right away.\n"
    "        Run 'git shared hook install' after changing it.\n"
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
    { "hook", MSG_USAGE_HOOK },
    { "link", MSG_USAGE_LINK },
    { "publish", MSG_USAGE_PUBLISH },
    { "set", MSG_USAGE_SET },
    { "status", MSG_USAGE_STATUS },
    { "update", MSG_USAGE_UPDATE },
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
