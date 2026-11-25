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
    MSG_USAGE_MV,
    MSG_USAGE_PRUNE,
    MSG_USAGE_BRANCH,
    MSG_USAGE_RELOCATE,
    MSG_USAGE_LANG,
    MSG_USAGE_PUBLISH,
    MSG_USAGE_UPDATE,
    MSG_USAGE_STATUS,
    MSG_USAGE_PUSH,
    MSG_USAGE_PULL,
    MSG_USAGE_CROWN,
    MSG_USAGE_HOOK,

    MSG_ERR_UNKNOWN_CMD,     /* sottocomando */
    MSG_ERR_UNKNOWN_TOPIC,   /* argomento di help */
    MSG_ERR_UNKNOWN_OPT,     /* opzione */
    MSG_ERR_OPT_VALUE,       /* opzione */
    MSG_ERR_TOO_MANY_ARGS,
    MSG_ERR_MISSING_PATH,
    MSG_ERR_MISSING_DEST,
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
    MSG_ERR_SRC_PENDING,     /* path, repo sorgente */
    MSG_ERR_SAME_REPO,
    MSG_ERR_HASH_MISMATCH,   /* algoritmo sorgente, algoritmo destinazione */
    MSG_ERR_DEST_PATH,       /* path */
    MSG_ERR_DEST_EXISTS,     /* path */
    MSG_ERR_ALREADY_LINKED,  /* nome, id */
    MSG_ERR_RELINK_PATH,     /* nome, path */
    MSG_ERR_RELINK_OPTS,     /* path */
    MSG_ERR_DEST_SHARED,     /* path, nome */
    MSG_ERR_ADOPT_NOT_SHARED, /* path */
    MSG_ERR_FORCE_MERGE,
    MSG_ERR_LINK_NO_BASE,    /* path */
    MSG_ERR_MV_SAME,         /* path */
    MSG_ERR_MV_NO_DIR,       /* cartella */
    MSG_ERR_MV_MERGING,      /* path, path */
    MSG_ERR_MV_MISSING,      /* path, destinazione */
    MSG_ERR_PENDING_OP,      /* path */
    MSG_ERR_UNLINK_UNCOMMITTED, /* path */
    MSG_ERR_UNLINK_ELSEWHERE, /* id, branch */
    MSG_ERR_PRUNE,           /* file, peer */
    MSG_ERR_NOT_LINKED,      /* path */
    MSG_ERR_BRANCH_RESET_ALONE,
    MSG_ERR_BRANCH_DEFAULT_OP,
    MSG_ERR_BRANCH_COMMIT,
    MSG_ERR_BRANCH_PATTERN,  /* branch */
    MSG_ERR_BRANCH_NAME,     /* branch */
    MSG_ERR_BRANCH_NOT_IN,   /* file, branch */
    MSG_ERR_BRANCH_WRITE,    /* file */
    MSG_ERR_BRANCH_DETACHED, /* file */
    MSG_ERR_BRANCH_LAST,     /* file */
    MSG_ERR_RELOCATE_NOTHING,
    MSG_ERR_LANG,            /* lingua */
    MSG_ERR_LANG_WRITE,
    MSG_ERR_RELOCATE_WHERE,
    MSG_ERR_RELOCATE_SAME,   /* path */
    MSG_ERR_RELOCATE_EXISTS, /* vecchio path, vecchio path */
    MSG_ERR_RELOCATE_GONE,   /* vecchio path, vecchio path */
    MSG_ERR_RELOCATE_NOT_MOVED, /* path */
    MSG_ERR_RELOCATE_NO_PEER, /* path */
    MSG_ERR_RELOCATE_NOT_DIR, /* path */
    MSG_ERR_RELOCATE_NOT_REPO, /* path */
    MSG_ERR_WRITE_FILE,      /* path */
    MSG_ERR_NOT_SHARED_PATH, /* path */
    MSG_ERR_LOCAL_CHANGES,   /* path */
    MSG_ERR_PEER_REJECTED,   /* file, peer */
    MSG_ERR_TOO_MANY_PEERS,  /* file, massimo (unsigned) */
    MSG_ERR_PUBLISH,         /* file */
    MSG_ERR_MERGE,           /* file */
    MSG_ERR_CONFLICT_OPEN,   /* file, path */
    MSG_ERR_CONFLICT_BINARY, /* file, path, path */
    MSG_ERR_CHOOSE_STAGED,   /* file, path */
    MSG_ERR_NO_MERGE,        /* file */
    MSG_ERR_NO_MERGE_ANY,
    MSG_ERR_ABORT_OPTS,
    MSG_ERR_CHOOSE_OPTS,
    MSG_ERR_NO_REMOTE,       /* remote */
    MSG_ERR_REMOTE_FETCH,    /* id, url */
    MSG_ERR_NO_CREATOR,      /* id, url */
    MSG_ERR_BAD_ID,          /* id */
    MSG_ERR_CREATOR_PUSH,    /* file, url */
    MSG_ERR_CREATOR_NEWER,   /* file, ruolo, url */
    MSG_ERR_CREATOR_REFUSED, /* file, ruolo, url */
    MSG_ERR_CREATOR_MISSING, /* file, ruolo, url */
    MSG_ERR_CREATOR_UNREACHABLE, /* file, ruolo, url */
    MSG_ERR_AUTH_DECIDE_FIRST, /* file, url, [url] path (D-111) */
    MSG_ERR_PUSH_NO_CREATOR, /* file, path */
    MSG_ERR_PUSH_NOTHING,
    MSG_ERR_PULL_NO_CREATOR, /* file, path */

    MSG_WARN_BAD_ENTRY,      /* nome della sezione */
    MSG_WARN_NOT_IN_HEAD,    /* path */
    MSG_WARN_NO_BRANCH,      /* nome, id */
    MSG_WARN_DISCARDED,      /* path (o id) */
    MSG_WARN_PENDING_NO_BRANCH, /* path (o id), branch, id */
    MSG_WARN_PEER_UNREACHABLE, /* file, peer */
    MSG_WARN_SUSPENDED,      /* file */
    MSG_WARN_HOOK,           /* path dell'hook */
    MSG_WARN_HOOK_NOT_SHELL, /* path dell'hook */
    MSG_WARN_HOOK_REPAIRED,  /* path dell'hook */
    MSG_WARN_HOOK_PENDING_NOT_SHELL, /* path dell'hook */
    MSG_WARN_MERGE_DROPPED,  /* file */
    MSG_WARN_PRUNE_KEPT,     /* file, peer */
    MSG_WARN_BRANCH_MISSING, /* branch */
    MSG_WARN_MOVED,          /* path ricordato */
    MSG_WARN_BRANCHES_GONE,  /* file, branch attivi, path */
    MSG_WARN_RELOCATE_NOT_MEMBER, /* file, path */
    MSG_WARN_AUTH_PENDING,   /* file, url, [url] path (D-111) */
    MSG_WARN_COPY_FAILED,    /* file, url */
    MSG_WARN_CHAIN_RESTORED, /* file, da dove (D-112, B-51) */
    MSG_HOOK_NOT_ALIGNED,    /* file, stato, comando */
    MSG_HOOK_ANNOUNCED,      /* file */
    MSG_HOOKS_INSTALLED,

    MSG_SET_STAGED,          /* path, nome */
    MSG_SET_DONE,            /* path, nome */
    MSG_SET_COMMIT_MSG,      /* path */
    MSG_LINK_STAGED,         /* path, nome, repo sorgente */
    MSG_LINK_DONE,           /* path, nome, repo sorgente */
    MSG_LINK_COMMIT_MSG,     /* path */
    MSG_LINK_KEPT,           /* path */
    MSG_LINK_NO_BASE_NEXT,   /* path */
    MSG_LINK_ADOPTED,        /* path, vecchio id */
    MSG_MV_STAGED,           /* path, destinazione */
    MSG_MV_DONE,             /* path, destinazione */
    MSG_MV_COMMIT_MSG,       /* path, destinazione */
    MSG_UNLINK_STAGED,       /* path */
    MSG_UNLINK_DONE,         /* path */
    MSG_UNLINK_COMMIT_MSG,   /* path */
    MSG_UNLINK_ORPHAN,       /* id */
    MSG_PRUNE_GONE,          /* file, peer */
    MSG_PRUNE_LEFT,          /* file, peer */
    MSG_PRUNE_REF,           /* ref */
    MSG_PRUNE_WOULD_GONE,    /* file, peer */
    MSG_PRUNE_WOULD_LEFT,    /* file, peer */
    MSG_PRUNE_WOULD_REF,     /* ref */
    MSG_PRUNE_NOTHING,
    MSG_BRANCH_FIELD_ACTIVE,
    MSG_BRANCH_FIELD_DEFAULT,
    MSG_BRANCH_CURRENT,
    MSG_BRANCH_MISSING,
    MSG_BRANCH_NONE,
    MSG_BRANCH_NOT_LINKED,
    MSG_BRANCH_ADDED,        /* file, branch */
    MSG_BRANCH_REMOVED,      /* file, branch */
    MSG_BRANCH_DEFAULT_ADDED, /* file, branch */
    MSG_BRANCH_DEFAULT_REMOVED, /* file, branch */
    MSG_BRANCH_DEFAULT_STAGED,
    MSG_BRANCH_RESET,        /* file */
    MSG_BRANCH_COMMIT_MSG,   /* path */
    MSG_RELOCATE_DONE,       /* file, peer raggiunti, peer */
    MSG_RELOCATE_COPY,       /* file, peer raggiunti, peer */
    MSG_RELOCATE_PEER,       /* file, vecchio path, nuovo path */
    MSG_RELOCATE_SELF,       /* path */
    MSG_LANG_CURRENT,        /* lingua */
    MSG_LANG_SET,            /* lingua */
    MSG_PUB_CREATED,         /* file */
    MSG_PUB_NOTIFIED,        /* file, peer raggiunti (unsigned), peer (unsigned) */
    MSG_PUB_NOTIFIED_SOME,   /* file, raggiunti, da avvisare, gia' aggiornati (unsigned) */
    MSG_PUB_UP_TO_DATE,      /* file */
    MSG_PUB_NO_PEERS,        /* file */
    MSG_PUB_TO_APPLY,        /* file */
    MSG_PUB_DIVERGED,        /* file */
    MSG_PUB_MERGING,         /* file */
    MSG_PUB_TO_MERGE,        /* file */
    MSG_PUB_NOT_ON_CREATOR,  /* file, ruolo */
    MSG_PUB_COPIED,          /* file, url */
    MSG_PUSH_CREATOR_NEW,    /* file, url */
    MSG_PUSH_HEIR_NEW,       /* file, url, posizione, autorita' (D-114) */
    MSG_PUSH_HEIR_NOTE,      /* file, ruolo, url, posizione, path */
    MSG_PULL_HEIR_FOLLOWED,  /* file, posizione, url, autorita' di prima */
    MSG_PUSH_TO_CREATOR,     /* file, ruolo, url */
    MSG_PUSH_ON_CREATOR,     /* file, ruolo, url */
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
    MSG_UPD_ABORTED,         /* file */

    MSG_STATUS_NONE,
    MSG_STATUS_FIELD_PATH,
    MSG_STATUS_FIELD_ID,
    MSG_STATUS_FIELD_STATE,
    MSG_STATUS_UNDECLARED,
    MSG_STATUS_SUSPENDED,    /* stato ricavato */
    MSG_STATUS_PENDING_ON,   /* stato, branch */
    MSG_STATUS_FIELD_NOTE,
    MSG_STATUS_FIELD_PEERS,
    MSG_STATUS_FIELD_CREATOR,
    MSG_STATUS_FIELD_AUTHORITY,
    MSG_STATUS_FIELD_HEIRS,  /* D-114 */
    MSG_ROLE_CREATOR,        /* ruolo dell'autorita' nei messaggi (D-111) */
    MSG_ROLE_SOVEREIGN,
    MSG_PEER_SAME,
    MSG_PEER_BEHIND,
    MSG_PEER_NEWER,
    MSG_PEER_NO_VERSION,
    MSG_PEER_ANNOUNCED,
    MSG_PEER_LEFT,
    MSG_PEER_GONE,
    MSG_PEER_UNREACHABLE,
    MSG_STATUS_BRANCHES_GONE, /* branch attivi, path */
    MSG_STATUS_ELSEWHERE,    /* branch */
    MSG_STATUS_ANNOUNCED,    /* repo che ha annunciato */
    MSG_STATUS_TO_MERGE,     /* repo che ha annunciato */
    MSG_STATUS_RENAMED,      /* path di prima */
    MSG_STATUS_CREATOR_UNREACHABLE, /* ruolo, url */
    MSG_STATUS_HEIR_FOUND,   /* url, posizione, autorita', path (D-114) */
    MSG_STATUS_HEIR_OTHER,   /* url, autorita', autorita', path */
    MSG_STATUS_CREATOR_BACK, /* url del creatore, path */
    MSG_STATUS_HEIRS_NONE,   /* path, path */
    MSG_LINK_COPY,           /* url, autorita' (D-111) */
    MSG_STATUS_CHAIN_MISSING, /* path (D-112, B-51) */
    MSG_CREATOR_SEEN,        /* data (D-112, B-53) */
    MSG_AUTH_CHANGE,         /* url, provenienza, versione (D-111) */
    MSG_AUTH_BY_PEER,        /* peer */
    MSG_AUTH_BY_AUTHORITY,
    MSG_AUTH_BY_UNKNOWN,
    MSG_AUTH_NEW_CREATOR,    /* url */
    MSG_AUTH_HOW,            /* [url] path */
    MSG_CREATOR_SAME,
    MSG_CREATOR_AHEAD,
    MSG_CREATOR_BEHIND,      /* ruolo */
    MSG_CREATOR_DIVERGED,
    MSG_CREATOR_UNSEEN,
    MSG_CREATOR_MISSING,

    /* crown (D-111) */
    MSG_ERR_CROWN_ARGS,
    MSG_ERR_HEIRS_NONE,      /* file, path, path (D-114) */
    MSG_ERR_HEIR_OTHER,      /* file, url, autorita', autorita', path */
    MSG_ERR_CREATOR_BACK,    /* file, url del creatore, path */
    MSG_ERR_CROWN_NO_CREATOR,     /* file */
    MSG_ERR_CROWN_MERGING,        /* file */
    MSG_ERR_CROWN_BAD_URL,        /* argomento */
    MSG_ERR_CROWN_BAD_CHAIN,      /* file, url */
    MSG_ERR_CROWN_OTHER_CREATOR,  /* file, url, creatore */
    MSG_ERR_CROWN_PUSH,           /* file, url */
    MSG_ERR_CROWN_RESTORE,        /* file, url del creatore */
    MSG_ERR_CROWN_NO_TTY,         /* file */
    MSG_ERR_NO_AUTH_CHANGE,       /* file */
    MSG_ERR_AUTH_CHANGE_UNKNOWN,  /* file, url */
    MSG_ERR_AUTH_CHANGES_URL,     /* file, quanti, opzione, [url] path */
    MSG_WARN_CROWN_OLD_NOT_TOLD,  /* file, url */
    MSG_CROWN_ALREADY,            /* file, url */
    MSG_CROWN_RESTORE_ALREADY,    /* file, url */
    MSG_CROWN_LAST_SEEN,          /* data */
    MSG_CROWN_NEVER_SEEN,
    MSG_CROWN_NOTICE_FROM,        /* file, da, quando, a */
    MSG_CROWN_NOTICE_LINE,        /* url */
    MSG_CROWN_NOTICE_FOLLOW,      /* url */
    MSG_CROWN_NOTICE_INSTALL,     /* url */
    MSG_CROWN_NOTICE_LOST,        /* url, quante */
    MSG_CROWN_NOTICE_PEERS,       /* quanti */
    MSG_CROWN_ACCEPT_NOTICE,      /* file, da, a, provenienza, versione */
    MSG_CROWN_CONTINUE,
    MSG_CROWN_CANCELLED,          /* file */
    MSG_CROWN_FOLLOWED,           /* file, url */
    MSG_CROWN_FOLLOW_AHEAD,       /* file, url */
    MSG_CROWN_TO_MERGE,           /* file */
    MSG_CROWN_INSTALLED,          /* file, url */
    MSG_CROWN_OLD_TOLD,           /* file, url */
    MSG_CROWN_ACCEPTED,           /* file, url */
    MSG_CROWN_REJECTED,           /* file, url */

    MSG_STATE_ALIGNED,
    MSG_STATE_TO_APPLY,
    MSG_STATE_TO_PUBLISH,
    MSG_STATE_DIVERGED,
    MSG_STATE_TO_COMMIT,
    MSG_STATE_MERGING,
    MSG_STATE_PENDING,
    MSG_STATE_UNLINKED,
    MSG_STATE_INCONSISTENT,
    MSG_STATE_ABSENT,

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
