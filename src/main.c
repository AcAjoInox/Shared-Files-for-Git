/*
 * main.c - punto d'ingresso di git shared (D-067).
 *
 * git lancia questo eseguibile per "git shared <args>" (§6.1). Qui si
 * gestiscono le opzioni comuni e si passa il controllo al sottocomando.
 */
#include <stdio.h>
#include <string.h>

#include "cmd.h"
#include "err.h"
#include "msg.h"
#include "sys.h"

typedef int (*cmd_fn_t)(int argc, char **argv);

typedef struct {
    const char *name;
    cmd_fn_t fn;
    eMsgId_t usage;
} CmdDef_t;

static const CmdDef_t commands[] = {
    { "help", cmd_help, MSG_USAGE_HELP },
    { "set", cmd_set, MSG_USAGE_SET },
    { "status", cmd_status, MSG_USAGE_STATUS },
};

static const CmdDef_t *find_command(const char *name)
{
    const CmdDef_t *found = NULL;
    size_t i;

    for (i = 0U; (i < (sizeof commands / sizeof commands[0])) &&
                 (found == NULL);
         i++) {
        if (strcmp(name, commands[i].name) == 0) {
            found = &commands[i];
        }
    }
    return found;
}

int main(int argc, char **argv)
{
    static char help_name[] = "help";
    static char *help_argv[] = { help_name, NULL };
    const CmdDef_t *cmd;
    int rc = GS_EXIT_USAGE;

    sys_init(&argc, &argv);

    if ((argc < 2) || (strcmp(argv[1], "-h") == 0)) {
        fputs(msg(MSG_USAGE_MAIN), stdout);
        goto exit;
    }
    /* "--help" arriva qui solo se l'eseguibile e' lanciato direttamente:
     * "git shared --help" e' intercettato da git (§6.1, REQ-F-019). */
    if (strcmp(argv[1], "--help") == 0) {
        rc = cmd_help(1, help_argv);
        goto exit;
    }
    cmd = find_command(argv[1]);
    if (cmd == NULL) {
        msg_error(MSG_ERR_UNKNOWN_CMD, argv[1]);
        fputs(msg(MSG_USAGE_MAIN), stderr);
        goto exit;
    }
    if ((argc > 2) && (strcmp(argv[2], "-h") == 0)) {
        fputs(msg(cmd->usage), stdout);
        goto exit;
    }
    rc = cmd->fn(argc - 1, argv + 1);

exit:
    if (fflush(stdout) != 0) {
        rc = GS_EXIT_FATAL; /* es. stdout chiuso o disco pieno */
    }
    return rc;
}
