/*
 * embed.c - strumento di build: le pagine di aiuto di help/ (testo
 * semplice, una per comando) diventano
 *
 *   embed -c <out.c> <file.txt>...   un sorgente C con tutte le pagine,
 *                                    incorporate nell'eseguibile
 *   embed -html <out.html> <file.txt> la pagina HTML che Git for Windows
 *                                    apre per "git shared --help"
 *   embed -man <out.1> <file.txt>    la man page che git apre su Linux
 *
 * Un solo sorgente per tutto (REQ-F-019). Nessuna dipendenza oltre al
 * compilatore C: gira dove gira il Makefile. Non fa parte di git-shared.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* C99/C11 garantiscono stringhe letterali fino a 4095 caratteri: ogni
 * pagina si divide in parti piu' corte, a fine riga. */
#define PART_MAX 3000U

typedef struct {
    char *buf;
    size_t len;
} Text_t;

static void die(const char *what, const char *path)
{
    fprintf(stderr, "embed: %s: %s\n", what, path);
    exit(1);
}

/* Contenuto del file, senza '\r'. */
static Text_t read_text(const char *path)
{
    Text_t t = { NULL, 0U };
    size_t cap = 0U;
    int c;
    FILE *f = fopen(path, "rb");

    if (f == NULL) {
        die("cannot read", path);
    }
    while ((c = getc(f)) != EOF) {
        if (c == '\r') {
            continue;
        }
        if ((t.len + 1U) >= cap) {
            cap = (cap == 0U) ? 4096U : (cap * 2U);
            t.buf = realloc(t.buf, cap);
            if (t.buf == NULL) {
                die("out of memory", path);
            }
        }
        t.buf[t.len++] = (char)c;
    }
    (void)fclose(f);
    if (t.buf == NULL) {
        t.buf = malloc(1U);
        if (t.buf == NULL) {
            die("out of memory", path);
        }
    }
    t.buf[t.len] = '\0';
    return t;
}

static FILE *open_out(const char *path)
{
    FILE *f = fopen(path, "wb");

    if (f == NULL) {
        die("cannot write", path);
    }
    return f;
}

static void close_out(FILE *f, const char *path)
{
    if ((ferror(f) != 0) || (fclose(f) != 0)) {
        die("cannot write", path);
    }
}

/* Nome della pagina: il nome del file senza cartella e senza ".txt". */
static void page_name(const char *path, char *out, size_t size)
{
    const char *base = path;
    const char *c;
    size_t n;

    for (c = path; *c != '\0'; c++) {
        if ((*c == '/') || (*c == '\\')) {
            base = c + 1;
        }
    }
    n = strlen(base);
    if ((n > 4U) && (strcmp(base + n - 4U, ".txt") == 0)) {
        n -= 4U;
    }
    if (n >= size) {
        die("name too long", path);
    }
    memcpy(out, base, n);
    out[n] = '\0';
}

/* Un carattere dentro una stringa letterale C. */
static void put_c_char(FILE *f, unsigned char c)
{
    if (c == '\n') {
        fputs("\\n", f);
    } else if ((c == '\\') || (c == '"')) {
        fprintf(f, "\\%c", c);
    } else if (c == '?') {
        fputs("\\?", f); /* niente trigrafi */
    } else if ((c < 0x20U) || (c >= 0x7FU)) {
        fprintf(f, "\\%03o", (unsigned)c);
    } else {
        fputc((int)c, f);
    }
}

static void write_c(const char *out, int n, char **files)
{
    char name[128];
    FILE *f = open_out(out);
    Text_t t;
    size_t i;
    size_t part;
    size_t line_len;
    const char *nl;
    int k;

    fputs("/* Generato da tools/embed.c dai file di help/: non modificare. */\n"
          "#include \"help_text.h\"\n\n", f);
    for (k = 0; k < n; k++) {
        t = read_text(files[k]);
        fprintf(f, "static const char *const page_%d[] = {\n    \"", k);
        part = 0U;
        for (i = 0U; i < t.len; i += line_len) {
            nl = memchr(t.buf + i, '\n', t.len - i);
            line_len = (nl != NULL) ? (size_t)(nl - (t.buf + i)) + 1U : (t.len - i);
            if (((part + line_len) > PART_MAX) && (part > 0U)) {
                fputs("\",\n    \"", f);
                part = 0U;
            }
            for (size_t j = 0U; j < line_len; j++) {
                put_c_char(f, (unsigned char)t.buf[i + j]);
            }
            part += line_len;
        }
        fputs("\",\n    NULL\n};\n\n", f);
        free(t.buf);
    }
    fputs("const HelpPage_t help_pages[] = {\n", f);
    for (k = 0; k < n; k++) {
        page_name(files[k], name, sizeof name);
        fprintf(f, "    { \"%s\", page_%d },\n", name, k);
    }
    fprintf(f, "};\n\nconst size_t help_page_count = %d;\n", n);
    close_out(f, out);
}

static void write_html(const char *out, const char *in)
{
    Text_t t = read_text(in);
    FILE *f = open_out(out);
    char name[128];
    size_t i;

    page_name(in, name, sizeof name);
    fprintf(f, "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n"
               "<title>%s(1)</title>\n</head>\n<body>\n<pre>\n", name);
    for (i = 0U; i < t.len; i++) {
        switch (t.buf[i]) {
        case '&': fputs("&amp;", f); break;
        case '<': fputs("&lt;", f); break;
        case '>': fputs("&gt;", f); break;
        default: fputc(t.buf[i], f); break;
        }
    }
    fputs("</pre>\n</body>\n</html>\n", f);
    close_out(f, out);
    free(t.buf);
}

static void write_man(const char *out, const char *in)
{
    Text_t t = read_text(in);
    FILE *f = open_out(out);
    char name[128];
    size_t i;
    int bol = 1;

    page_name(in, name, sizeof name);
    for (i = 0U; name[i] != '\0'; i++) {
        name[i] = (char)toupper((unsigned char)name[i]);
    }
    fprintf(f, ".TH %s 1\n.nf\n", name);
    for (i = 0U; i < t.len; i++) {
        if (bol && ((t.buf[i] == '.') || (t.buf[i] == '\''))) {
            fputs("\\&", f); /* una riga che inizia con . non e' un comando */
        }
        if (t.buf[i] == '\\') {
            fputs("\\e", f);
        } else {
            fputc(t.buf[i], f);
        }
        bol = (t.buf[i] == '\n');
    }
    fputs(".fi\n", f);
    close_out(f, out);
    free(t.buf);
}

int main(int argc, char **argv)
{
    if ((argc >= 4) && (strcmp(argv[1], "-c") == 0)) {
        write_c(argv[2], argc - 3, argv + 3);
    } else if ((argc == 4) && (strcmp(argv[1], "-html") == 0)) {
        write_html(argv[2], argv[3]);
    } else if ((argc == 4) && (strcmp(argv[1], "-man") == 0)) {
        write_man(argv[2], argv[3]);
    } else {
        fputs("usage: embed -c <out.c> <file.txt>...\n"
              "   or: embed -html <out.html> <file.txt>\n"
              "   or: embed -man <out.1> <file.txt>\n", stderr);
        return 129;
    }
    return 0;
}
