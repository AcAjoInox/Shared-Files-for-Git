/*
 * cmd_status.c - "git shared status" (REQ-F-012).
 *
 * M0: elenca le voci di .gitshared. Lo stato non e' ancora ricavato.
 */
#include <stdio.h>

#include "cmd.h"
#include "err.h"
#include "git.h"
#include "msg.h"
#include "shared.h"

static void print_entry(const SharedEntry_t *e)
{
    printf("%s\n", str_cstr(&e->name));
    printf("    %-6s %s\n", msg(MSG_STATUS_FIELD_PATH), str_cstr(&e->path));
    printf("    %-6s %s\n", msg(MSG_STATUS_FIELD_ID), str_cstr(&e->id));
    printf("    %-6s %s\n", msg(MSG_STATUS_FIELD_STATE),
           msg(MSG_STATE_UNKNOWN));
}

int cmd_status(int argc, char **argv)
{
    Str_t top = STR_INIT;
    SharedList_t list = SHARED_LIST_INIT;
    size_t i;
    size_t shown = 0U;
    gs_err_t err;
    int rc = GS_EXIT_FATAL;

    (void)argv;
    if (argc > 1) {
        msg_error(MSG_ERR_TOO_MANY_ARGS);
        fputs(msg(MSG_USAGE_STATUS), stderr);
        rc = GS_EXIT_USAGE;
        goto exit;
    }

    err = git_toplevel(&top);
    if (err == GS_ERR_NOT_REPO) {
        msg_fatal(MSG_ERR_NOT_REPO);
        goto exit;
    }
    if (err != GS_OK) {
        msg_fatal(MSG_ERR_PROC, "git");
        goto exit;
    }

    err = shared_load(str_cstr(&top), &list);
    if (err != GS_OK) {
        msg_fatal(MSG_ERR_READ_GITSHARED);
        goto exit;
    }

    for (i = 0U; i < list.n; i++) {
        if (shared_entry_valid(&list.v[i]) != 0) {
            if (shown > 0U) {
                putchar('\n');
            }
            print_entry(&list.v[i]);
            shown++;
        } else {
            msg_warning(MSG_WARN_BAD_ENTRY, str_cstr(&list.v[i].name));
        }
    }
    if (shown == 0U) {
        puts(msg(MSG_STATUS_NONE));
    }
    rc = GS_EXIT_OK;

exit:
    shared_list_free(&list);
    str_free(&top);
    return rc;
}
