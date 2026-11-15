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
#define GS_ID_HEX_LEN 32U   /* cifre esadecimali dell'ID (128 bit, D-006) */

#endif /* GS_BOUNDS_H */
