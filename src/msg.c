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
        "    unlink    stop sharing a file\n"
        "    mv        rename a shared file\n"
        "    status    show the shared files of this repository\n"
        "    publish   send new versions to the peers\n"
        "    update    get the new versions and apply them\n"
        "    prune     forget the peers that no longer exist\n"
        "    branch    choose the branches where a file is shared\n"
        "    relocate  tell the peers that this repository was moved\n"
        "    lang      choose the language of the manual: en or it\n"
        "    help      show the manual\n"
        "\n"
        "'git shared <command> -h' shows the options of a command.\n"
        "\n"
        "The manual:\n"
        "    git shared help                overview and list of commands\n"
        "    git shared --help              the same overview, opened by git\n"
        "                                   (installed by make install)\n"
        "    git shared <command> --help    page of a command, for example\n"
        "                                   'git shared link --help'\n"
        "    git shared help <command>      the same page\n"
        "\n"
        "In English or Italian (manuale in italiano):\n"
        "    git shared lang it\n"
        "    git shared lang en\n",
    [MSG_USAGE_HELP] =
        "usage: git shared help [<command>]\n",
    [MSG_USAGE_SET] =
        "usage: git shared set [-c | -m <message>] [--] <path>\n"
        "\n"
        "    -c, --commit      commit .gitshared right away\n"
        "    -m <message>      commit .gitshared right away with <message>\n",
    [MSG_USAGE_LINK] =
        "usage: git shared link [-c | -m <message>] [-f | --merge] [--adopt]\n"
        "                       [--] <source-file> [<destination>]\n"
        "   or: git shared link [-c | -m <message>] [-f | --merge] [--adopt]\n"
        "                       [--] <url> <id> [<destination>]\n"
        "\n"
        "    <source-file>     the shared file in the other repository\n"
        "    <url> <id>        a file published to a remote: its creator\n"
        "                      (or a copy) and the ID of the file\n"
        "    <destination>     path in this repository "
        "(default: as in the source)\n"
        "    -c, --commit      commit right away\n"
        "    -m <message>      commit right away with <message>\n"
        "    -f, --force       overwrite an existing <destination> with the\n"
        "                      shared version\n"
        "    --merge           link an existing <destination> that has\n"
        "                      nothing in common with the shared file;\n"
        "                      'git shared update' merges them\n"
        "    --adopt           <destination> is already shared with another\n"
        "                      ID: take the ID of the source\n",
    [MSG_USAGE_MV] =
        "usage: git shared mv [-c | -m <message>] [--] <path> <destination>\n"
        "\n"
        "Rename a shared file. Only here: the peers keep their own path.\n"
        "\n"
        "    -c, --commit      commit the rename right away\n"
        "    -m <message>      commit the rename right away with <message>\n",
    [MSG_USAGE_UNLINK] =
        "usage: git shared unlink [-c | -m <message>] [--] <path>\n"
        "\n"
        "Stop sharing <path>. The file stays, as a regular file.\n"
        "\n"
        "    -c, --commit      commit .gitshared right away\n"
        "    -m <message>      commit .gitshared right away with <message>\n",
    [MSG_USAGE_PRUNE] =
        "usage: git shared prune [-n] [<path>]\n"
        "\n"
        "Forget the peers that no longer exist or no longer share the file.\n"
        "\n"
        "    -n, --dry-run     only show what would be removed\n",
    [MSG_USAGE_BRANCH] =
        "usage: git shared branch [<path>]\n"
        "   or: git shared branch <path> (-a | -rm) <branch>\n"
        "   or: git shared branch <path> --reset\n"
        "   or: git shared branch <path> --default (-a | -rm) <branch>\n"
        "                                [-c | -m <message>]\n"
        "\n"
        "Choose the branches where <path> is shared. On the other branches\n"
        "it is a regular file: no warnings, nothing published.\n"
        "\n"
        "    -a, --add <branch>       share the file on <branch> too\n"
        "    -rm, --remove <branch>   stop sharing the file on <branch>\n"
        "    --reset                  go back to the default list\n"
        "    --default                change the default list in .gitshared,\n"
        "                             the one copied by set, link and clones\n"
        "    -c, -m <message>         with --default: commit .gitshared\n"
        "                             right away\n",
    [MSG_USAGE_RELOCATE] =
        "usage: git shared relocate [<old-path>]\n"
        "   or: git shared relocate --copy [<old-path>]\n"
        "   or: git shared relocate --peer <old-path> <new-path>\n"
        "\n"
        "Tell the peers that this repository was moved or copied.\n"
        "\n"
        "    <old-path>   where it was (default: the path it remembers)\n"
        "    --copy       it is a copy of <old-path>: share from both\n"
        "    --peer       a peer was moved: update its path here\n",
    [MSG_USAGE_LANG] =
        "usage: git shared lang [en | it]\n"
        "\n"
        "Show or choose the language of the manual, for all your\n"
        "repositories. Messages stay in English for now.\n",
    [MSG_USAGE_PUBLISH] =
        "usage: git shared publish [--remote <remote>] [<path>]\n"
        "\n"
        "    --remote <remote>  push the versions to this remote; the first\n"
        "                       time, it becomes the creator of the file\n"
        "                       (default for the copy: origin)\n",
    [MSG_USAGE_UPDATE] =
        "usage: git shared update [-f] [--all] [-c | -m <message>] [<path>]\n"
        "   or: git shared update --abort [<path>]\n"
        "\n"
        "Diverged versions are merged. After a conflict: fix the file,\n"
        "'git add' it and run 'git shared update' again. To give up:\n"
        "'git shared update --abort'.\n"
        "\n"
        "    -f, --force       overwrite local changes\n"
        "    --all             fetch from every peer, not only from the\n"
        "                      ones that announced a new version\n"
        "    -c, --commit      commit the applied files right away\n"
        "    -m <message>      commit right away with <message>\n"
        "    --abort           give up the merge in progress\n",
    [MSG_USAGE_STATUS] =
        "usage: git shared status [--peers]\n"
        "\n"
        "Show the state of each shared file.\n"
        "\n"
        "    --peers    also show whether each peer has the same version\n",
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
    [MSG_ERR_MISSING_DEST] = "missing <destination>",
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
    [MSG_ERR_SRC_PENDING] =
        "'%s' is not shared yet in %s: commit .gitshared there, "
        "then run link again",
    [MSG_ERR_SAME_REPO] = "source and destination are the same repository",
    [MSG_ERR_HASH_MISMATCH] =
        "the repositories use different hash algorithms (%s, %s)",
    [MSG_ERR_DEST_PATH] = "invalid destination path: '%s'",
    [MSG_ERR_DEST_EXISTS] = "'%s' already exists",
    [MSG_ERR_ALREADY_LINKED] = "'%s' is already shared here (id %s)",
    [MSG_ERR_PENDING_OP] =
        "'%s' has a pending operation: commit .gitshared first, or restore it",
    [MSG_ERR_UNLINK_UNCOMMITTED] =
        "'%s' is not committed in .gitshared on this branch: commit it first",
    [MSG_ERR_UNLINK_ELSEWHERE] =
        "%s is still shared on branch '%s': run unlink there",
    [MSG_ERR_PRUNE] = "%s: cannot remove peer %s",
    [MSG_ERR_NOT_LINKED] =
        "'%s' is not linked here: run 'git shared link' first",
    [MSG_ERR_BRANCH_RESET_ALONE] =
        "--reset cannot be used with --add, --remove or --default",
    [MSG_ERR_BRANCH_DEFAULT_OP] = "--default needs --add or --remove",
    [MSG_ERR_BRANCH_COMMIT] = "-c and -m can only be used with --default",
    [MSG_ERR_BRANCH_PATTERN] =
        "branch patterns are not supported yet: '%s' (use exact names)",
    [MSG_ERR_BRANCH_NAME] = "invalid branch name: '%s'",
    [MSG_ERR_BRANCH_NOT_IN] = "%s: '%s' is not in the list",
    [MSG_ERR_BRANCH_WRITE] = "%s: cannot change the active branches",
    [MSG_ERR_BRANCH_LAST] =
        "%s: it must stay active on at least one branch "
        "(to stop sharing it: git shared unlink)",
    [MSG_ERR_LANG] = "unknown language: '%s' (use en or it)",
    [MSG_ERR_LANG_WRITE] = "cannot write shared.lang in the global git config",
    [MSG_ERR_RELOCATE_NOTHING] = "no file is linked here: nothing to relocate",
    [MSG_ERR_RELOCATE_WHERE] =
        "this repository does not remember where it was: give <old-path>",
    [MSG_ERR_RELOCATE_SAME] = "this repository is still at %s",
    [MSG_ERR_RELOCATE_EXISTS] =
        "%s still contains a repository: if this one is a copy, "
        "run 'git shared relocate --copy %s'",
    [MSG_ERR_RELOCATE_GONE] =
        "no repository at %s anymore: if this one was moved from there, "
        "run 'git shared relocate %s'",
    [MSG_ERR_RELOCATE_NOT_MOVED] = "%s still contains a repository: it was not moved",
    [MSG_ERR_RELOCATE_NO_PEER] = "no shared file here has %s as a peer",
    [MSG_ERR_RELOCATE_NOT_DIR] =
        "'%s' is not a folder: --peer wants the new path of a moved "
        "repository, not of a file",
    [MSG_ERR_RELOCATE_NOT_REPO] =
        "'%s' is not a git repository: --peer wants the new path of a moved "
        "repository",
    [MSG_ERR_BRANCH_DETACHED] =
        "%s: no default in .gitshared and HEAD is detached: "
        "use --add <branch>",
    [MSG_ERR_RELINK_PATH] =
        "'%s' is declared here at '%s': run link without <destination>",
    [MSG_ERR_RELINK_OPTS] =
        "'%s' is already declared here and is linked as it is: "
        "-f, --merge and --adopt are not used",
    [MSG_ERR_DEST_SHARED] =
        "'%s' is already shared here as '%s', with another ID: "
        "use --adopt to take the ID of the source",
    [MSG_ERR_ADOPT_NOT_SHARED] =
        "'%s' is not shared here: --adopt is not needed",
    [MSG_ERR_FORCE_MERGE] = "-f and --merge cannot be used together",
    [MSG_ERR_LINK_NO_BASE] =
        "'%s' already exists and has nothing in common with the shared "
        "file: use --merge to link it and merge them with 'git shared "
        "update', or -f to overwrite it",
    [MSG_ERR_MV_SAME] = "'%s' is already there",
    [MSG_ERR_MV_NO_DIR] = "the folder '%s' does not exist: create it first",
    [MSG_ERR_MV_MERGING] =
        "'%s' has a merge in progress: finish it with 'git shared update', "
        "or give it up with 'git shared update --abort %s'",
    [MSG_ERR_MV_MISSING] =
        "'%s' is not in the index, and '%s' neither: nothing to rename",
    [MSG_ERR_WRITE_FILE] = "cannot write '%s'",
    [MSG_ERR_NOT_SHARED_PATH] = "'%s' is not a shared file",
    [MSG_ERR_LOCAL_CHANGES] =
        "'%s' has local changes: commit or discard them, or use -f",
    [MSG_ERR_PEER_REJECTED] =
        "%s: rejected by %s, which has a newer version: "
        "run 'git shared update'",
    [MSG_ERR_TOO_MANY_PEERS] = "%s: too many peers (at most %u)",
    [MSG_ERR_PUBLISH] = "%s: cannot create the new version",
    [MSG_ERR_MERGE] = "%s: cannot merge",
    [MSG_ERR_CONFLICT_OPEN] =
        "%s: merge conflict: fix it, 'git add' it, then run "
        "'git shared update' (to give up: git shared update --abort '%s')",
    [MSG_ERR_NO_MERGE] = "%s: no merge in progress",
    [MSG_ERR_NO_MERGE_ANY] = "no merge in progress",
    [MSG_ERR_ABORT_OPTS] = "--abort cannot be used with -c, -m, -f or --all",
    [MSG_ERR_NO_REMOTE] = "no remote named '%s' in this repository",
    [MSG_ERR_REMOTE_FETCH] =
        "cannot get '%s' from %s: is the file shared there, and can the "
        "repository be reached?",
    [MSG_ERR_NO_CREATOR] =
        "'%s' in %s was never published to a remote: link it with the path "
        "of the file",
    [MSG_ERR_BAD_ID] = "'%s' is not a valid ID",
    [MSG_ERR_CREATOR_PUSH] =
        "%s: cannot push to %s: this repository did not become the creator",
    [MSG_ERR_CREATOR_NEWER] =
        "%s: the creator %s has a newer version: run 'git shared update'",
    [MSG_ERR_CREATOR_REFUSED] =
        "%s: not accepted by the creator %s (no write access, or not "
        "reachable): the version stays here as a proposal",

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
    [MSG_WARN_HOOK_NOT_SHELL] =
        "%s is not a shell script: left as it is (no git shared warnings "
        "from it: run 'git shared status' after checkout and merge)",
    [MSG_WARN_HOOK_REPAIRED] =
        "%s is not a shell script: removed the git shared block that an "
        "earlier version had added",
    [MSG_WARN_HOOK_PENDING_NOT_SHELL] =
        "%s is not a shell script: left as it is (the pending operation "
        "completes at the next 'git shared' command, not at the commit)",
    [MSG_WARN_MERGE_DROPPED] =
        "%s: the merge in progress was abandoned: dropped",
    [MSG_WARN_PRUNE_KEPT] =
        "%s: peer %s cannot be read now: kept",
    [MSG_WARN_BRANCH_MISSING] = "no branch named '%s' yet",
    [MSG_WARN_MOVED] =
        "this repository was at %s: if it was moved, run 'git shared "
        "relocate'; if it is a copy, 'git shared relocate --copy'",
    [MSG_WARN_BRANCHES_GONE] =
        "%s: none of its active branches exists (%s): "
        "run 'git shared branch %s -a <branch>'",
    [MSG_WARN_RELOCATE_NOT_MEMBER] = "%s: %s does not share this file: skipped",
    [MSG_WARN_CREATOR_UNREACHABLE] =
        "%s: creator %s not reachable: using the versions in this repository",
    [MSG_WARN_COPY_FAILED] = "%s: cannot copy the versions to %s",
    [MSG_HOOK_NOT_ALIGNED] = "%s: %s (run 'git shared %s')",
    [MSG_HOOK_ANNOUNCED] =
        "%s: new version announced by a peer (run 'git shared update')",
    [MSG_HOOKS_INSTALLED] = "Hooks installed.",

    [MSG_SET_STAGED] =
        "'%s' is now shared as '%s'. Commit .gitshared to confirm.",
    [MSG_SET_DONE] = "'%s' is now shared as '%s'.",
    [MSG_SET_COMMIT_MSG] = "Share %s",
    [MSG_LINK_STAGED] =
        "'%s' linked as '%s' from %s. Commit it with .gitshared to confirm.",
    [MSG_LINK_DONE] = "'%s' linked as '%s' from %s.",
    [MSG_LINK_COMMIT_MSG] = "Link %s",
    [MSG_LINK_KEPT] =
        "%s: the existing file was kept ('git shared status' shows how it "
        "compares with the shared version)",
    [MSG_LINK_NO_BASE_NEXT] =
        "%s: nothing in common with the shared file: once committed, "
        "'git shared update' merges them (expect conflicts)",
    [MSG_LINK_ADOPTED] =
        "%s: takes the ID of the source; the old ID %s is unlinked by the "
        "same commit",
    [MSG_MV_STAGED] =
        "'%s' renamed to '%s'. Commit it with .gitshared to confirm.",
    [MSG_MV_DONE] = "'%s' renamed to '%s'.",
    [MSG_MV_COMMIT_MSG] = "Rename %s to %s",
    [MSG_UNLINK_STAGED] =
        "'%s' is no longer shared. Commit .gitshared to confirm.",
    [MSG_UNLINK_DONE] = "'%s' is no longer shared: it is now a regular file.",
    [MSG_UNLINK_COMMIT_MSG] = "Unshare %s",
    [MSG_UNLINK_ORPHAN] = "Removed the leftover local data of %s.",
    [MSG_PRUNE_GONE] = "%s: removed peer %s (it no longer exists)",
    [MSG_PRUNE_LEFT] = "%s: removed peer %s (it no longer shares the file)",
    [MSG_PRUNE_REF] = "removed leftover %s",
    [MSG_PRUNE_WOULD_GONE] = "%s: would remove peer %s (it no longer exists)",
    [MSG_PRUNE_WOULD_LEFT] =
        "%s: would remove peer %s (it no longer shares the file)",
    [MSG_PRUNE_WOULD_REF] = "would remove leftover %s",
    [MSG_PRUNE_NOTHING] = "Nothing to prune.",
    [MSG_BRANCH_FIELD_ACTIVE] = "active",
    [MSG_BRANCH_FIELD_DEFAULT] = "default",
    [MSG_BRANCH_CURRENT] = "current",
    [MSG_BRANCH_MISSING] = "no such branch",
    [MSG_BRANCH_NONE] = "(none)",
    [MSG_BRANCH_NOT_LINKED] = "(not linked here)",
    [MSG_BRANCH_ADDED] = "%s: active on '%s'",
    [MSG_BRANCH_REMOVED] = "%s: no longer active on '%s'",
    [MSG_BRANCH_DEFAULT_ADDED] = "%s: '%s' added to the default",
    [MSG_BRANCH_DEFAULT_REMOVED] = "%s: '%s' removed from the default",
    [MSG_BRANCH_DEFAULT_STAGED] = "Commit .gitshared to confirm.",
    [MSG_BRANCH_RESET] = "%s: active branches reset to the default",
    [MSG_BRANCH_COMMIT_MSG] = "Change the default branches of %s",
    [MSG_RELOCATE_DONE] = "%s: %u of %u peers know the new path",
    [MSG_RELOCATE_COPY] = "%s: shared from here too (%u of %u peers told)",
    [MSG_RELOCATE_PEER] = "%s: peer %s is now %s",
    [MSG_RELOCATE_SELF] = "This repository is now known at %s.",
    [MSG_LANG_CURRENT] = "%s",
    [MSG_LANG_SET] = "Manual language: %s.",
    [MSG_PUB_CREATED] = "%s: new version created",
    [MSG_PUB_NOTIFIED] = "%s: %u of %u peers notified",
    [MSG_PUB_NO_PEERS] = "%s: no peers to send it to",
    [MSG_PUB_TO_APPLY] =
        "%s: a newer version exists: run 'git shared update' first",
    [MSG_PUB_DIVERGED] =
        "%s: diverged from the latest version: run 'git shared update'",
    [MSG_PUB_MERGING] =
        "%s: merge in progress: run 'git shared update' first",
    [MSG_PUB_TO_MERGE] =
        "%s: a version from a peer does not follow this one: run "
        "'git shared update' to merge it first",
    [MSG_PUB_CREATOR_NEW] =
        "%s: published to %s, which is now the creator of the file",
    [MSG_PUB_COPIED] = "%s: versions copied to %s",
    [MSG_PUB_TO_CREATOR] = "%s: published to the creator %s",
    [MSG_PUB_CREATOR_HAS] = "%s: the creator %s already has this version",
    [MSG_SKIP_STATE] = "%s: %s, nothing to do",
    [MSG_UPD_RECEIVED] = "%s: new version received",
    [MSG_UPD_ABORTED] =
        "%s: merge abandoned, the file is back to the last commit",
    [MSG_UPD_APPLIED] = "%s: new version applied. Commit it to confirm.",
    [MSG_UPD_COMMITTED] = "%s: new version applied and committed.",
    [MSG_UPD_UP_TO_DATE] = "%s: up to date",
    [MSG_UPD_TO_PUBLISH] =
        "%s: local change not published yet: run 'git shared publish'",
    [MSG_UPD_MERGED] =
        "%s: merged. Commit it to confirm, then run 'git shared publish'.",
    [MSG_UPD_MERGED_COMMITTED] =
        "%s: merged and committed. Run 'git shared publish' to send it.",
    [MSG_UPD_MERGED_PUBLISH] =
        "%s: merged, nothing to commit. Run 'git shared publish' to send it.",
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
    [MSG_STATUS_FIELD_NOTE] = "note",
    [MSG_STATUS_FIELD_PEERS] = "peers",
    [MSG_STATUS_FIELD_CREATOR] = "creator",
    [MSG_PEER_SAME] = "same version",
    [MSG_PEER_BEHIND] = "older version: run 'git shared publish'",
    [MSG_PEER_NEWER] =
        "newer or different version: run 'git shared update --all'",
    [MSG_PEER_NO_VERSION] = "no version yet: run 'git shared publish'",
    [MSG_PEER_ANNOUNCED] = "announced, not received yet: nothing to do",
    [MSG_PEER_LEFT] = "no longer shares the file: run 'git shared prune'",
    [MSG_PEER_GONE] = "not found: if it was deleted, run 'git shared prune'",
    [MSG_PEER_UNREACHABLE] = "cannot be read now",
    [MSG_STATUS_ELSEWHERE] =
        "shared on branch '%s', not on this one: nothing to do here",
    [MSG_STATUS_ANNOUNCED] =
        "new version announced by %s, not received: "
        "run 'git shared update'",
    [MSG_STATUS_TO_MERGE] =
        "the version of %s must be merged: run 'git shared update'",
    [MSG_STATUS_CREATOR_UNREACHABLE] =
        "creator %s not reachable: state from the versions in this repository",
    [MSG_STATUS_RENAMED] =
        "renamed from '%s': commit it with .gitshared to confirm",
    [MSG_STATUS_BRANCHES_GONE] =
        "none of its active branches exists (%s): "
        "git shared branch %s -a <branch>",

    [MSG_STATE_ALIGNED] = "aligned",
    [MSG_STATE_TO_APPLY] = "new version to apply",
    [MSG_STATE_TO_PUBLISH] = "to publish",
    [MSG_STATE_DIVERGED] = "diverged",
    [MSG_STATE_TO_COMMIT] = "applied, to commit",
    [MSG_STATE_MERGING] = "merge in progress",
    [MSG_STATE_PENDING] = "pending commit",
    [MSG_STATE_UNLINKED] = "unlinked",
    [MSG_STATE_INCONSISTENT] = "inconsistent",
    [MSG_STATE_ABSENT] = "not declared here",
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
