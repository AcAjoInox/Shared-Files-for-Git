/*
 * sys_win.h - conversioni UTF-8 <-> UTF-16, solo per i file *_win.c.
 */
#ifndef GS_SYS_WIN_H
#define GS_SYS_WIN_H

#include <wchar.h>

/* Stringhe allocate con xmalloc, da liberare con free. */
wchar_t *win_utf8_to_wide(const char *s);
char *win_wide_to_utf8(const wchar_t *w);

#endif /* GS_SYS_WIN_H */
