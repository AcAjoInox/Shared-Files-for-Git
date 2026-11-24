/*
 * bounds.h - limiti del sistema (D-034, REQ-NF-011).
 *
 * Unico posto in cui sono definiti. Il superamento di un limite produce
 * un errore esplicito, mai un troncamento silenzioso.
 */
#ifndef GS_BOUNDS_H
#define GS_BOUNDS_H

#define GS_PATH_MAX   4096U /* caratteri di un path o di un URL */
#define GS_PEER_MAX   32U   /* peer per file shared */
#define GS_SOVEREIGN_MAX 16U /* sovrani nella voce "sovereigns" (D-111) */
#define GS_ID_HEX_LEN 32U   /* cifre esadecimali dell'ID (128 bit, D-006) */
#define GS_ID_RAW_LEN 16U   /* byte dell'ID */

/* Oid: 20 byte con SHA-1, 32 con SHA-256 (D-033). Tenuti in binario. */
#define GS_OID_RAW_SHA1 20U
#define GS_OID_RAW_MAX  32U
#define GS_OID_HEX_MAX  (2U * GS_OID_RAW_MAX)

#endif /* GS_BOUNDS_H */
