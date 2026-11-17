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

/* In parti: una stringa letterale oltre 4095 caratteri non e' garantita
 * da C99/C11 (-Woverlength-strings). */
static const char *const manual[] = {
    "GIT-SHARED(1)\n"
    "\n"
    "NAME\n"
    "    git-shared - keep the same file aligned across repositories\n"
    "\n"
    "SYNOPSIS\n"
    "    git shared set [-c | -m <message>] [--] <path>\n"
    "    git shared link [-c | -m <message>] [--] <source-file> "
    "[<destination>]\n"
    "    git shared unlink [-c | -m <message>] [--] <path>\n"
    "    git shared status\n"
    "    git shared publish [<path>]\n"
    "    git shared update [-f] [-c | -m <message>] [<path>]\n"
    "    git shared prune [-n] [<path>]\n"
    "    git shared hook install\n"
    "    git shared help [<command>]\n"
    "\n"
    "DESCRIPTION\n"
    "    A shared file is a regular file tracked by several repositories.\n"
    "    Each repository keeps a real copy of it; git shared tells every\n"
    "    repository when a new version exists and applies it on request.\n"
    "\n"
    ,
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
    "    unlink [-c | -m <message>] [--] <path>\n"
    "        Stop sharing a file. The file stays, as a regular file. When\n"
    "        .gitshared is committed, the other repositories forget this\n"
    "        one. To cancel before the commit: git restore .gitshared.\n"
    "\n"
    "    publish [<path>]\n"
    "        For each shared file changed and committed on top of the\n"
    "        latest version, create a new version and send it to the\n"
    "        peers. This is the only way to publish: committing never\n"
    "        sends anything. A peer that already has a newer version\n"
    "        refuses it: run update first.\n"
    "\n"
    "    update [-f] [-c | -m <message>] [<path>]\n"
    "        Get the versions from the peers and apply the latest one to\n"
    "        the working tree and the index. A local change made on top\n"
    "        of an older version, or two versions published by peers that\n"
    "        did not see each other, are merged: the merge is a new\n"
    "        version, to commit and then publish. On a conflict the file\n"
    "        gets the usual markers: fix it, 'git add' it and run update\n"
    "        again; to give up, restore the file with git restore.\n"
    "        Local changes are never\n"
    "        overwritten, unless -f is given; a version applied by an\n"
    "        earlier update and not committed yet is not a local change.\n"
    "        Nothing is committed by default.\n"
    "\n"
    "    prune [-n] [<path>]\n"
    "        Forget the peers that were deleted, or that no longer share\n"
    "        the file, here and in the other peers. A peer that cannot be\n"
    "        read right now is kept. -n only shows what would be removed.\n"
    "\n"
    "    status\n"
    "        Show each shared file with its state: aligned, new version to\n"
    "        apply, applied (to commit), to publish, diverged, merge in\n"
    "        progress, pending\n"
    "        commit, unlinked, inconsistent. On a branch where the file is\n"
    "        not active the state is shown as suspended.\n"
    "\n"
    "    hook install\n"
    "        Install the hooks in a repository whose shared files have none\n"
    "        yet, and remove the blocks no longer used (pre-commit and\n"
    "        post-commit of earlier versions). set and link install them by\n"
    "        themselves.\n"
    "\n"
    "    help [<command>]\n"
    "        Show this manual, or the usage of a command.\n"
    "\n"
    ,
    "HOOKS\n"
    "    set and link add a block to some hooks. Existing hooks are kept:\n"
    "    the block goes right after their first line.\n"
    "      post-checkout, post-merge, post-rewrite\n"
    "                     warn when a shared file is not aligned\n"
    "    No hook runs on commit and no hook ever blocks git. A set or link\n"
    "    is confirmed by the first git shared command after its commit.\n"
    "    The hooks are a convenience: status shows the same information.\n"
    "\n"
    "FILES\n"
    "    .gitshared\n"
    "        Versioned file at the root of the repository that declares\n"
    "        the shared files. Managed by git shared: do not edit by hand.\n"
    "\n"
    "EXIT STATUS\n"
    "    0 on success, 1 on error, 128 on fatal error, 129 on wrong usage.\n"
};

/* Uso di un sottocomando, per "git shared help <command>". */
static const struct {
    const char *name;
    eMsgId_t usage;
} topics[] = {
    { "help", MSG_USAGE_HELP },
    { "hook", MSG_USAGE_HOOK },
    { "link", MSG_USAGE_LINK },
    { "prune", MSG_USAGE_PRUNE },
    { "publish", MSG_USAGE_PUBLISH },
    { "set", MSG_USAGE_SET },
    { "status", MSG_USAGE_STATUS },
    { "unlink", MSG_USAGE_UNLINK },
    { "update", MSG_USAGE_UPDATE },
};

int cmd_help(int argc, char **argv)
{
    size_t i;
    int rc = GS_EXIT_USAGE;

    if (argc < 2) {
        for (i = 0U; i < (sizeof manual / sizeof manual[0]); i++) {
            fputs(manual[i], stdout);
        }
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
