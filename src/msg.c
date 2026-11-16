/*
 * msg.c - tabelle dei messaggi.
 */
#include "msg.h"

#include <stdarg.h>
#include <stdio.h>

static const char *const msg_en[] = {
    [MSG_USAGE_MAIN] =
        "usage: git shared <command> [<args>]\n"
        "\n"
        "    set       declare a file of this repository as shared\n"
        "    link      link a shared file of another repository\n"
        "    status    show the shared files of this repository\n"
        "    publish   send new versions to the peers\n"
        "    update    get the new versions and apply them\n"
        "    help      show the manual\n"
        "\n"
        "'git shared <command> -h' shows the options of a command.\n",
    [MSG_USAGE_HELP] =
        "usage: git shared help [<command>]\n",
    [MSG_USAGE_SET] =
        "usage: git shared set [-c | -m <message>] [--] <path>\n"
        "\n"
        "    -c, --commit      commit .gitshared right away\n"
        "    -m <message>      commit .gitshared right away with <message>\n",
    [MSG_USAGE_LINK] =
        "usage: git shared link [-c | -m <message>] [--] <source-file> "
        "[<destination>]\n"
        "\n"
        "    <source-file>     the shared file in the other repository\n"
        "    <destination>     path in this repository "
        "(default: as in the source)\n"
        "    -c, --commit      commit right away\n"
        "    -m <message>      commit right away with <message>\n",
    [MSG_USAGE_PUBLISH] =
        "usage: git shared publish [<path>]\n",
    [MSG_USAGE_UPDATE] =
        "usage: git shared update [-f] [-c | -m <message>] [<path>]\n"
        "\n"
        "    -f, --force       overwrite local changes\n"
        "    -c, --commit      commit the applied files right away\n"
        "    -m <message>      commit right away with <message>\n",
    [MSG_USAGE_STATUS] =
        "usage: git shared status\n",
    [MSG_USAGE_HOOK] =
        "usage: git shared hook install\n"
        "   or: git shared hook <event> [<args>]\n"
        "\n"
        "    install    install the hooks in a repository whose shared files\n"
        "               have none yet, and remove the ones no longer used\n"
        "    <event>    called by the hooks; not meant to be run by hand\n",

    [MSG_ERR_UNKNOWN_CMD] = "unknown subcommand: '%s'",
    [MSG_ERR_UNKNOWN_TOPIC] = "no help for '%s'",
    [MSG_ERR_UNKNOWN_OPT] = "unknown option: '%s'",
    [MSG_ERR_OPT_VALUE] = "option '%s' requires a value",
    [MSG_ERR_TOO_MANY_ARGS] = "too many arguments",
    [MSG_ERR_MISSING_PATH] = "missing <path>",
    [MSG_ERR_NOT_REPO] =
        "not a git repository (or not inside its working tree)",
    [MSG_ERR_PROC] = "cannot run '%s'",
    [MSG_ERR_READ_GITSHARED] = "cannot read '.gitshared'",
    [MSG_ERR_WRITE_GITSHARED] = "cannot write '.gitshared'",
    [MSG_ERR_STAGE_GITSHARED] =
        "cannot stage '.gitshared': add it by hand with 'git add .gitshared'",
    [MSG_ERR_PATH_NEWLINE] = "paths with a newline are not supported",
    [MSG_ERR_IS_DIR] = "'%s' is a directory: only single files can be shared",
    [MSG_ERR_NOT_TRACKED] = "'%s' is not tracked by git: add it first",
    [MSG_ERR_CONFLICT] = "'%s' has unresolved conflicts",
    [MSG_ERR_NOT_REGULAR] = "'%s' is not a regular file",
    [MSG_ERR_ALREADY_SHARED] = "'%s' is already shared as '%s'",
    [MSG_ERR_NAME_USED] = "a shared file named '%s' already exists",
    [MSG_ERR_RANDOM] = "cannot generate a random ID",
    [MSG_ERR_PENDING] = "cannot complete a pending operation",
    [MSG_ERR_COMMIT] =
        "commit failed: .gitshared is staged, commit it by hand",
    [MSG_ERR_CHAIN] = "cannot update the version chain of '%s'",
    [MSG_ERR_SRC_NOT_REPO] = "'%s' is not inside a git repository",
    [MSG_ERR_SRC_NOT_SHARED] =
        "'%s' is not shared in its repository: run 'git shared set' there",
    [MSG_ERR_SRC_NO_VERSIONS] =
        "cannot get the versions of '%s' from %s: is it committed there?",
    [MSG_ERR_SAME_REPO] = "source and destination are the same repository",
    [MSG_ERR_HASH_MISMATCH] =
        "the repositories use different hash algorithms (%s, %s)",
    [MSG_ERR_DEST_PATH] = "invalid destination path: '%s'",
    [MSG_ERR_DEST_EXISTS] =
        "'%s' already exists: linking an existing file is not supported yet",
    [MSG_ERR_ALREADY_LINKED] = "'%s' is already shared here (id %s)",
    [MSG_ERR_WRITE_FILE] = "cannot write '%s'",
    [MSG_ERR_NOT_SHARED_PATH] = "'%s' is not a shared file",
    [MSG_ERR_LOCAL_CHANGES] =
        "'%s' has local changes: commit or discard them, or use -f",
    [MSG_ERR_PEER_REJECTED] =
        "%s: rejected by %s, which has a newer version: "
        "run 'git shared update'",
    [MSG_ERR_TOO_MANY_PEERS] = "%s: too many peers (at most %u)",
    [MSG_ERR_PUBLISH] = "%s: cannot create the new version",

    [MSG_WARN_BAD_ENTRY] = "ignoring invalid entry '%s' in .gitshared",
    [MSG_WARN_NOT_IN_HEAD] =
        "'%s' is not committed yet: the shared file is waiting for it",
    [MSG_WARN_NO_BRANCH] =
        "HEAD is detached: '%s' has no active branch "
        "(add one with: git config --add shared.%s.branch <branch>)",
    [MSG_WARN_DISCARDED] =
        "discarded pending operation on '%s': .gitshared was restored",
    [MSG_WARN_PENDING_NO_BRANCH] =
        "'%s' is waiting for a commit on branch '%s', which no longer "
        "exists (to drop it: git config --remove-section shared.%s)",
    [MSG_WARN_PEER_UNREACHABLE] = "%s: peer %s not reachable",
    [MSG_WARN_SUSPENDED] = "%s: suspended on this branch",
    [MSG_WARN_HOOK] = "cannot install the hook '%s'",
    [MSG_HOOK_NOT_ALIGNED] = "%s: %s (run 'git shared %s')",
    [MSG_HOOKS_INSTALLED] = "Hooks installed.",

    [MSG_SET_STAGED] =
        "'%s' is now shared as '%s'. Commit .gitshared to confirm.",
    [MSG_SET_DONE] = "'%s' is now shared as '%s'.",
    [MSG_SET_COMMIT_MSG] = "Share %s",
    [MSG_LINK_STAGED] =
        "'%s' linked as '%s' from %s. Commit it with .gitshared to confirm.",
    [MSG_LINK_DONE] = "'%s' linked as '%s' from %s.",
    [MSG_LINK_COMMIT_MSG] = "Link %s",
    [MSG_PUB_CREATED] = "%s: new version created",
    [MSG_PUB_PUSHED] = "%s: sent to %u of %u peers",
    [MSG_PUB_NO_PEERS] = "%s: no peers to send it to",
    [MSG_PUB_TO_APPLY] =
        "%s: a newer version exists: run 'git shared update' first",
    [MSG_PUB_DIVERGED] =
        "%s: diverged from the latest version: run 'git shared update'",
    [MSG_SKIP_STATE] = "%s: %s, nothing to do",
    [MSG_UPD_RECEIVED] = "%s: new version received",
    [MSG_UPD_APPLIED] = "%s: new version applied. Commit it to confirm.",
    [MSG_UPD_COMMITTED] = "%s: new version applied and committed.",
    [MSG_UPD_UP_TO_DATE] = "%s: up to date",
    [MSG_UPD_TO_PUBLISH] =
        "%s: local change not published yet: run 'git shared publish'",
    [MSG_UPD_DIVERGED] = "%s: diverged: merging is not implemented yet",
    [MSG_UPD_TO_COMMIT] =
        "%s: the latest version is applied but not committed: commit it "
        "to confirm",
    [MSG_UPD_COMMIT_MSG] = "Update %s",

    [MSG_STATUS_NONE] = "No shared files in this repository.",
    [MSG_STATUS_FIELD_PATH] = "path",
    [MSG_STATUS_FIELD_ID] = "id",
    [MSG_STATUS_FIELD_STATE] = "state",
    [MSG_STATUS_UNDECLARED] = "(not declared in .gitshared)",
    [MSG_STATUS_SUSPENDED] = "suspended on this branch (%s)",
    [MSG_STATUS_PENDING_ON] = "%s (on branch %s)",

    [MSG_STATE_ALIGNED] = "aligned",
    [MSG_STATE_TO_APPLY] = "new version to apply",
    [MSG_STATE_TO_PUBLISH] = "to publish",
    [MSG_STATE_DIVERGED] = "diverged",
    [MSG_STATE_TO_COMMIT] = "applied, to commit",
    [MSG_STATE_PENDING] = "pending commit",
    [MSG_STATE_UNLINKED] = "unlinked",
    [MSG_STATE_INCONSISTENT] = "inconsistent",
};

_Static_assert(sizeof msg_en / sizeof msg_en[0] == MSG_COUNT,
               "msg_en: one text per message ID");

const char *msg(eMsgId_t id)
{
    const char *s = NULL;

    if ((unsigned)id < (unsigned)MSG_COUNT) {
        s = msg_en[id];
    }
    /* Un buco nella tabella e' un errore di programmazione: lo si rende
     * visibile invece di passare NULL a printf. */
    return (s != NULL) ? s : "(missing message)";
}

static void vreport(const char *prefix, eMsgId_t id, va_list ap)
{
    fputs(prefix, stderr);
    (void)vfprintf(stderr, msg(id), ap);
    fputc('\n', stderr);
}

void msg_fatal(eMsgId_t id, ...)
{
    va_list ap;

    va_start(ap, id);
    vreport("fatal: ", id, ap);
    va_end(ap);
}

void msg_error(eMsgId_t id, ...)
{
    va_list ap;

    va_start(ap, id);
    vreport("error: ", id, ap);
    va_end(ap);
}

void msg_warning(eMsgId_t id, ...)
{
    va_list ap;

    va_start(ap, id);
    vreport("warning: ", id, ap);
    va_end(ap);
}
