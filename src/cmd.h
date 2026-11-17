/*
 * cmd.h - sottocomandi di git shared.
 *
 * Ogni sottocomando riceve argv a partire dal proprio nome (argv[0]) e
 * restituisce il codice di uscita del programma (GS_EXIT_*). L'opzione -h
 * e' gestita da main per tutti i sottocomandi.
 */
#ifndef GS_CMD_H
#define GS_CMD_H

int cmd_branch(int argc, char **argv);
int cmd_help(int argc, char **argv);
int cmd_hook(int argc, char **argv);
int cmd_lang(int argc, char **argv);
int cmd_link(int argc, char **argv);
int cmd_prune(int argc, char **argv);
int cmd_publish(int argc, char **argv);
int cmd_relocate(int argc, char **argv);
int cmd_set(int argc, char **argv);
int cmd_status(int argc, char **argv);
int cmd_unlink(int argc, char **argv);
int cmd_update(int argc, char **argv);

#endif /* GS_CMD_H */
