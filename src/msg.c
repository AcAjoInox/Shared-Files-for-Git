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
        "    status    show the shared files of this repository\n"
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
    [MSG_USAGE_STATUS] =
        "usage: git shared status\n",

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
    [MSG_ERR_CHAIN] = "cannot create the version chain of '%s'",

    [MSG_WARN_BAD_ENTRY] = "ignoring invalid entry '%s' in .gitshared",
    [MSG_WARN_NOT_IN_HEAD] =
        "'%s' is not committed yet: the shared file is waiting for it",
    [MSG_WARN_NO_BRANCH] =
        "HEAD is detached: '%s' has no active branch "
        "(add one with: git config --add shared.%s.branch <branch>)",
    [MSG_WARN_DISCARDED] =
        "discarded pending operation on %s: .gitshared was restored",

    [MSG_SET_STAGED] =
        "'%s' is now shared as '%s'. Commit .gitshared to confirm.",
    [MSG_SET_DONE] = "'%s' is now shared as '%s'.",
    [MSG_SET_COMMIT_MSG] = "Share %s",

    [MSG_STATUS_NONE] = "No shared files in this repository.",
    [MSG_STATUS_FIELD_PATH] = "path",
    [MSG_STATUS_FIELD_ID] = "id",
    [MSG_STATUS_FIELD_STATE] = "state",
    [MSG_STATUS_UNDECLARED] = "(not declared in .gitshared)",
    [MSG_STATUS_SUSPENDED] = "suspended on this branch (%s)",

    [MSG_STATE_ALIGNED] = "aligned",
    [MSG_STATE_TO_APPLY] = "new version to apply",
    [MSG_STATE_TO_PUBLISH] = "to publish",
    [MSG_STATE_DIVERGED] = "diverged",
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
