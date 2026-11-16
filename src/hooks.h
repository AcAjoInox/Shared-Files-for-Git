/*
 * hooks.h - installazione degli hook (REQ-F-016, D-030, D-074, §6.3).
 *
 * In ogni hook va un blocco delimitato da marker che chiama
 * "git-shared hook <evento>". Gli hook esistenti non si sovrascrivono mai:
 * il blocco si inserisce subito dopo la prima riga ("#!..."), perche' un
 * hook che finisce con "exit" non lo raggiungerebbe in fondo.
 *
 * Hook installati (D-074):
 *   post-checkout, post-merge, post-rewrite   sempre (avvisi)
 *   pre-commit, post-commit                   mai: il blocco si toglie
 *                                             (versioni precedenti di M3)
 */
#ifndef GS_HOOKS_H
#define GS_HOOKS_H

#include "err.h"
#include "shared.h"

/*
 * Allinea gli hook: aggiunge i blocchi che servono e toglie quelli che non
 * servono piu' (pre-commit e post-commit delle versioni precedenti). Un
 * hook che non si puo' scrivere produce un avviso; GS_ERR_SYS se almeno
 * uno non e' andato.
 */
eGsErr_t hooks_sync(const Repo_t *r);

#endif /* GS_HOOKS_H */
