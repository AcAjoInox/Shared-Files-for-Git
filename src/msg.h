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
    MSG_USAGE_LINK,
    MSG_USAGE_UNLINK,
    MSG_USAGE_PUBLISH,
    MSG_USAGE_UPDATE,
    MSG_USAGE_STATUS,
    MSG_USAGE_HOOK,

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
    MSG_ERR_SRC_NOT_REPO,    /* path */
    MSG_ERR_SRC_NOT_SHARED,  /* path */
    MSG_ERR_SRC_NO_VERSIONS, /* path, repo sorgente */
    MSG_ERR_SAME_REPO,
    MSG_ERR_HASH_MISMATCH,   /* algoritmo sorgente, algoritmo destinazione */
    MSG_ERR_DEST_PATH,       /* path */
    MSG_ERR_DEST_EXISTS,     /* path */
    MSG_ERR_ALREADY_LINKED,  /* nome, id */
    MSG_ERR_RELINK_PATH,     /* nome, path */
    MSG_ERR_PENDING_OP,      /* path */
    MSG_ERR_UNLINK_UNCOMMITTED, /* path */
    MSG_ERR_UNLINK_ELSEWHERE, /* id, branch */
    MSG_ERR_WRITE_FILE,      /* path */
    MSG_ERR_NOT_SHARED_PATH, /* path */
    MSG_ERR_LOCAL_CHANGES,   /* path */
    MSG_ERR_PEER_REJECTED,   /* file, peer */
    MSG_ERR_TOO_MANY_PEERS,  /* file, massimo (unsigned) */
    MSG_ERR_PUBLISH,         /* file */
    MSG_ERR_MERGE,           /* file */
    MSG_ERR_CONFLICT_OPEN,   /* file, path */

    MSG_WARN_BAD_ENTRY,      /* nome della sezione */
    MSG_WARN_NOT_IN_HEAD,    /* path */
    MSG_WARN_NO_BRANCH,      /* nome, id */
    MSG_WARN_DISCARDED,      /* path (o id) */
    MSG_WARN_PENDING_NO_BRANCH, /* path (o id), branch, id */
    MSG_WARN_PEER_UNREACHABLE, /* file, peer */
    MSG_WARN_SUSPENDED,      /* file */
    MSG_WARN_HOOK,           /* path dell'hook */
    MSG_WARN_MERGE_DROPPED,  /* file */
    MSG_HOOK_NOT_ALIGNED,    /* file, stato, comando */
    MSG_HOOKS_INSTALLED,

    MSG_SET_STAGED,          /* path, nome */
    MSG_SET_DONE,            /* path, nome */
    MSG_SET_COMMIT_MSG,      /* path */
    MSG_LINK_STAGED,         /* path, nome, repo sorgente */
    MSG_LINK_DONE,           /* path, nome, repo sorgente */
    MSG_LINK_COMMIT_MSG,     /* path */
    MSG_UNLINK_STAGED,       /* path */
    MSG_UNLINK_DONE,         /* path */
    MSG_UNLINK_COMMIT_MSG,   /* path */
    MSG_UNLINK_ORPHAN,       /* id */
    MSG_PUB_CREATED,         /* file */
    MSG_PUB_PUSHED,          /* file, peer raggiunti (unsigned), peer (unsigned) */
    MSG_PUB_NO_PEERS,        /* file */
    MSG_PUB_TO_APPLY,        /* file */
    MSG_PUB_DIVERGED,        /* file */
    MSG_PUB_MERGING,         /* file */
    MSG_SKIP_STATE,          /* file, stato */
    MSG_UPD_RECEIVED,        /* file */
    MSG_UPD_APPLIED,         /* file */
    MSG_UPD_COMMITTED,       /* file */
    MSG_UPD_UP_TO_DATE,      /* file */
    MSG_UPD_TO_PUBLISH,      /* file */
    MSG_UPD_MERGED,          /* file */
    MSG_UPD_MERGED_COMMITTED, /* file */
    MSG_UPD_MERGED_PUBLISH,  /* file */
    MSG_UPD_TO_COMMIT,       /* file */
    MSG_UPD_COMMIT_MSG,      /* path o "shared files" */

    MSG_STATUS_NONE,
    MSG_STATUS_FIELD_PATH,
    MSG_STATUS_FIELD_ID,
    MSG_STATUS_FIELD_STATE,
    MSG_STATUS_UNDECLARED,
    MSG_STATUS_SUSPENDED,    /* stato ricavato */
    MSG_STATUS_PENDING_ON,   /* stato, branch */

    MSG_STATE_ALIGNED,
    MSG_STATE_TO_APPLY,
    MSG_STATE_TO_PUBLISH,
    MSG_STATE_DIVERGED,
    MSG_STATE_TO_COMMIT,
    MSG_STATE_MERGING,
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
