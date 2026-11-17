/*
 * help_text.h - pagine di aiuto incorporate (REQ-F-019, D-087).
 *
 * I testi stanno in help/, un file per comando (help/git-shared.txt e'
 * la pagina generale); la traduzione italiana in help/it/ (D-088).
 * Al build tools/embed.c li trasforma in build/help_text.c: la
 * definizione di help_pages non si scrive a mano.
 */
#ifndef GS_HELP_TEXT_H
#define GS_HELP_TEXT_H

#include <stddef.h>

typedef struct {
    const char *lang;         /* "en" (help/) o "it" (help/it/) */
    const char *name;         /* "git-shared", "set", "link", ... */
    const char *const *parts; /* parti del testo, terminate da NULL */
} HelpPage_t;

extern const HelpPage_t help_pages[];
extern const size_t help_page_count;

/* Lingua delle pagine: shared.lang ("en" o "it"); "en" se non e' scritta
 * o non e' valida (D-088). Letta una volta (cmd_help.c). */
const char *help_lang(void);

#endif /* GS_HELP_TEXT_H */
