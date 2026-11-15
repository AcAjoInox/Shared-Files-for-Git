/*
 * hooks.h - installazione degli hook (REQ-F-016, D-030, D-073, §6.3).
 *
 * In ogni hook va un blocco delimitato da marker che chiama
 * "git-shared hook <evento>". Gli hook esistenti non si sovrascrivono mai:
 * il blocco si inserisce subito dopo la prima riga ("#!..."), perche' un
 * hook che finisce con "exit" non lo raggiungerebbe in fondo.
 *
 * Hook installati (D-073):
 *   post-checkout, post-merge, post-rewrite   sempre (avvisi)
 *   post-commit                               solo con shared.autoPublish
 *   pre-commit                                mai: il blocco si toglie
 */
#ifndef GS_HOOKS_H
#define GS_HOOKS_H

#include "err.h"
#include "shared.h"

/*
 * Allinea gli hook alla configurazione: aggiunge i blocchi che servono e
 * toglie quelli che non servono piu' (es. post-commit dopo aver spento
 * shared.autoPublish). Un hook che non si puo' scrivere produce un
 * avviso; GS_ERR_SYS se almeno uno non e' andato.
 */
eGsErr_t hooks_sync(const Repo_t *r);

#endif /* GS_HOOKS_H */
