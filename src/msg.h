/*
 * msg.h - messaggi per l'utente (§8.5, REQ-NF-008).
 *
 * Ogni testo visibile all'utente ha un ID; i testi stanno in tabelle
 * statiche, una per lingua. Nella v0 c'e' solo l'inglese.
 */
#ifndef GS_MSG_H
#define GS_MSG_H

#include "str.h"

typedef enum {
    MSG_USAGE_MAIN,
    MSG_USAGE_HELP,
    MSG_USAGE_STATUS,
    MSG_ERR_UNKNOWN_CMD,   /* %s = sottocomando */
    MSG_ERR_UNKNOWN_TOPIC, /* %s = argomento di help */
    MSG_ERR_TOO_MANY_ARGS,
    MSG_ERR_NOT_REPO,
    MSG_ERR_PROC,          /* %s = programma */
    MSG_ERR_GIT,           /* %s = comando git */
    MSG_ERR_READ_GITSHARED,
    MSG_WARN_BAD_ENTRY,    /* %s = nome della sezione */
    MSG_STATUS_NONE,
    MSG_STATUS_FIELD_PATH,
    MSG_STATUS_FIELD_ID,
    MSG_STATUS_FIELD_STATE,
    MSG_STATE_UNKNOWN,
    MSG_COUNT
} msg_id_t;

/* Testo del messaggio nella lingua corrente. */
const char *msg(msg_id_t id);

/* Messaggi su stderr con il prefisso di git ("fatal: ", "error: ", ...).
 * Non terminano il programma: l'uscita resta al chiamante (§8.4). */
void msg_fatal(msg_id_t id, ...);
void msg_error(msg_id_t id, ...);
void msg_warning(msg_id_t id, ...);

#endif /* GS_MSG_H */
