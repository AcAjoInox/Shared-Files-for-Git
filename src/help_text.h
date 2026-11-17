/*
 * help_text.h - pagine di aiuto incorporate (REQ-F-019, D-087).
 *
 * I testi stanno in help/, un file per comando (help/git-shared.txt e'
 * la pagina generale). Al build tools/embed.c li trasforma in
 * build/help_text.c: la definizione di help_pages non si scrive a mano.
 */
#ifndef GS_HELP_TEXT_H
#define GS_HELP_TEXT_H

#include <stddef.h>

typedef struct {
    const char *name;         /* "git-shared", "set", "link", ... */
    const char *const *parts; /* parti del testo, terminate da NULL */
} HelpPage_t;

extern const HelpPage_t help_pages[];
extern const size_t help_page_count;

#endif /* GS_HELP_TEXT_H */
