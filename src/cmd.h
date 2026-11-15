/*
 * cmd.h - sottocomandi di git shared.
 *
 * Ogni sottocomando riceve argv a partire dal proprio nome (argv[0]) e
 * restituisce il codice di uscita del programma (GS_EXIT_*). L'opzione -h
 * e' gestita da main per tutti i sottocomandi.
 */
#ifndef GS_CMD_H
#define GS_CMD_H

int cmd_help(int argc, char **argv);
int cmd_set(int argc, char **argv);
int cmd_status(int argc, char **argv);

#endif /* GS_CMD_H */
