/*
 * msg.h - messaggi per l'utente (§8.5, REQ-NF-008).
 *
 * Ogni testo visibile all'utente ha un ID; i testi stanno in tabelle
 * statiche, una per lingua. Nella v0 c'e' solo l'inglese. Il commento
 * accanto all'ID elenca gli argomenti printf del testo.
 */
#ifndef GS_MSG_H
#define GS_MSG_H

#include "str.h"

typedef enum {
    MSG_USAGE_MAIN,
    MSG_USAGE_HELP,
    MSG_USAGE_SET,
    MSG_USAGE_STATUS,

    MSG_ERR_UNKNOWN_CMD,     /* sottocomando */
    MSG_ERR_UNKNOWN_TOPIC,   /* argomento di help */
    MSG_ERR_UNKNOWN_OPT,     /* opzione */
    MSG_ERR_OPT_VALUE,       /* opzione */
    MSG_ERR_TOO_MANY_ARGS,
    MSG_ERR_MISSING_PATH,
    MSG_ERR_NOT_REPO,
    MSG_ERR_PROC,            /* programma */
    MSG_ERR_READ_GITSHARED,
    MSG_ERR_WRITE_GITSHARED,
    MSG_ERR_STAGE_GITSHARED,
    MSG_ERR_PATH_NEWLINE,
    MSG_ERR_IS_DIR,          /* path */
    MSG_ERR_NOT_TRACKED,     /* path */
    MSG_ERR_CONFLICT,        /* path */
    MSG_ERR_NOT_REGULAR,     /* path */
    MSG_ERR_ALREADY_SHARED,  /* path, nome */
    MSG_ERR_NAME_USED,       /* nome */
    MSG_ERR_RANDOM,
    MSG_ERR_PENDING,
    MSG_ERR_COMMIT,
    MSG_ERR_CHAIN,           /* nome */

    MSG_WARN_BAD_ENTRY,      /* nome della sezione */
    MSG_WARN_NOT_IN_HEAD,    /* path */
    MSG_WARN_NO_BRANCH,      /* nome, id */
    MSG_WARN_DISCARDED,      /* id */

    MSG_SET_STAGED,          /* path, nome */
    MSG_SET_DONE,            /* path, nome */
    MSG_SET_COMMIT_MSG,      /* path */

    MSG_STATUS_NONE,
    MSG_STATUS_FIELD_PATH,
    MSG_STATUS_FIELD_ID,
    MSG_STATUS_FIELD_STATE,
    MSG_STATUS_UNDECLARED,
    MSG_STATUS_SUSPENDED,    /* stato ricavato */

    MSG_STATE_ALIGNED,
    MSG_STATE_TO_APPLY,
    MSG_STATE_TO_PUBLISH,
    MSG_STATE_DIVERGED,
    MSG_STATE_PENDING,
    MSG_STATE_UNLINKED,
    MSG_STATE_INCONSISTENT,

    MSG_COUNT
} eMsgId_t;

/* Testo del messaggio nella lingua corrente. */
const char *msg(eMsgId_t id);

/* Messaggi su stderr con il prefisso di git ("fatal: ", "error: ", ...).
 * Non terminano il programma: l'uscita resta al chiamante (§8.4). */
void msg_fatal(eMsgId_t id, ...);
void msg_error(eMsgId_t id, ...);
void msg_warning(eMsgId_t id, ...);

#endif /* GS_MSG_H */
