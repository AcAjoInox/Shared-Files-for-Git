/*
 * remote.c - la catena in remoto: il creatore.
 */
#include "remote.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "auth.h"
#include "bounds.h"
#include "chain.h"
#include "mem.h"
#include "msg.h"
#include "path.h"
#include "peers.h"
#include "sys.h"

static int url_is_path(const char *u);

/*
 * Un URL che arriva dalla catena e' scritto da altri: non deve poter
 * diventare un'opzione di git ("--upload-pack=<comando>") ne' spezzare le
 * righe. Si rifiutano un trattino in testa, i caratteri di controllo e gli
 * URL troppo lunghi (REQ-NF-011). Si mostra all'utente, che decide
 * guardandolo (crown --accept): solo caratteri stampabili, e fuori
 * dall'ASCII solo in un path locale (S-07). Un URL di rete con una lettera
 * cirillica al posto di una latina, o un controllo bidi, sembrerebbe
 * quello vero; un nome di host non ASCII si scrive in punycode.
 */
int remote_url_ok(const char *url)
{
    size_t n = strlen(url);
    size_t k;
    size_t len;
    int path = -1;

    if ((n == 0U) || (n > GS_PATH_MAX) || (url[0] == '-')) {
        return 0;
    }
    for (k = 0U; k < n; k += len) {
        len = str_char_printable(url + k);
        if (len == 0U) {
            return 0;
        }
        if ((len > 1U) && (path < 0)) {
            path = url_is_path(url);
        }
        if ((len > 1U) && (path == 0)) {
            return 0;
        }
    }
    return 1;
}

/*
 * Nei comandi di rete git non chiede credenziali sul terminale (D-112,
 * P-07): la rete l'ha chiesta l'utente, ma lo stderr di git e' scartato e
 * una domanda invisibile bloccherebbe il comando. Senza permessi il push
 * e' un rifiuto. Le credenziali salvate (credential helper) valgono come
 * sempre. Una volta per processo.
 * S-10: nemmeno Git Credential Manager apre la sua finestra
 * (GCM_INTERACTIVE=never), e ssh non chiede nulla (BatchMode: nessuna
 * domanda sulla chiave dell'host, nessuna passphrase) e non aspetta piu' di
 * 15 secondi la connessione; ssh solo se l'utente non ha scelto un suo
 * comando (GIT_SSH_COMMAND, GIT_SSH, core.sshCommand).
 */
static void no_prompt(void)
{
    static const char *const args[] = { "config", "core.sshCommand", NULL };
    static int done = 0;
    Str_t out = STR_INIT;
    char *v;
    int own = 0;

    if (done != 0) {
        return;
    }
    done = 1;
    (void)sys_setenv("GIT_TERMINAL_PROMPT", "0");
    (void)sys_setenv("GCM_INTERACTIVE", "never");
    v = sys_getenv("GIT_SSH_COMMAND");
    own = (v != NULL) && (v[0] != '\0');
    free(v);
    v = sys_getenv("GIT_SSH");
    own = own || ((v != NULL) && (v[0] != '\0'));
    free(v);
    if ((own == 0) && (git_run(args, PROC_STDERR_NULL, &out) == GS_OK) &&
        (out.len > 0U)) {
        own = 1;
    }
    if (own == 0) {
        (void)sys_setenv("GIT_SSH_COMMAND",
                         "ssh -o BatchMode=yes -o ConnectTimeout=15");
    }
    str_free(&out);
}

/*
 * Opzioni -c di ogni comando di rete verso un URL (S-09), prima del nome
 * del comando: oggetti controllati da git prima di entrare nella repo
 * (transfer.fsckObjects), e un server che manda meno di 1000 byte al
 * secondo per 30 secondi interrompe il trasferimento http invece di
 * bloccare il comando per sempre.
 */
#define NET_OPTS_N 6U
static const char *const net_opts[NET_OPTS_N] = {
    "-c", "transfer.fsckObjects=true",
    "-c", "http.lowSpeedLimit=1000",
    "-c", "http.lowSpeedTime=30"
};

/* Copia le opzioni di rete in testa ad args; ritorna quante. */
static size_t add_net_opts(const char **args)
{
    size_t k;

    for (k = 0U; k < NET_OPTS_N; k++) {
        args[k] = net_opts[k];
    }
    return NET_OPTS_N;
}

static int is_sep(char c)
{
    return (c == '/') || (c == '\\');
}

/* Lettera di unita' Windows in testa: "C:" seguito da un separatore o
 * dalla fine. */
static int is_drive(const char *u)
{
    return isalpha((unsigned char)u[0]) && (u[1] == ':') &&
           ((u[2] == '\0') || is_sep(u[2]));
}

/*
 * 1 se l'URL e' un path locale, con la regola di git (connect.c): niente
 * "://" e nessun ":" prima del primo separatore, salvo una lettera di
 * unita'. "host:path" e "user@host:path" sono ssh.
 */
static int url_is_path(const char *u)
{
    const char *colon = strchr(u, ':');
    const char *c;

    if (strstr(u, "://") != NULL) {
        return 0;
    }
    if ((colon == NULL) || is_drive(u)) {
        return 1;
    }
    for (c = u; c < colon; c++) {
        if (is_sep(*c)) {
            return 1;
        }
    }
    return 0;
}

/* base + rel con "." e ".." risolti, mantenendo i separatori iniziali di
 * base ("/home/..." e "//server/share/..."), che path_join_norm toglie. */
static int join_abs(const char *base, const char *rel, Str_t *out)
{
    Str_t tmp = STR_INIT;
    size_t lead = 0U;
    int ok;

    while (is_sep(base[lead])) {
        lead++;
    }
    ok = path_join_norm(base, rel, &tmp);
    str_reset(out);
    while (lead > 0U) {
        str_addch(out, '/');
        lead--;
    }
    str_add(out, str_cstr(&tmp));
    str_free(&tmp);
    return ok;
}

/*
 * URL senza credenziali (S-01): in "schema://utente:password@host/..." si
 * toglie tutto lo userinfo, che per http(s) e' spesso un token; per ssh si
 * tiene l'utente (git@), che serve e non e' un segreto, e si toglie solo
 * ":password". La forma scp (utente@host:path) e i path restano come sono.
 * 1 se ha tolto qualcosa.
 */
static int url_public(const char *u, Str_t *out)
{
    static const char *const ssh[] = { "ssh", "git+ssh", "ssh+git" };
    const char *sep = strstr(u, "://");
    const char *auth;
    const char *end;
    const char *at = NULL;
    const char *colon;
    const char *p;
    size_t slen;
    size_t k;
    int is_ssh = 0;

    str_reset(out);
    if (sep == NULL) {
        str_add(out, u);
        return 0;
    }
    auth = sep + 3;
    end = auth + strcspn(auth, "/?#");
    for (p = auth; p < end; p++) {
        if (*p == '@') {
            at = p; /* l'ultima: una password puo' contenere '@' */
        }
    }
    if (at == NULL) {
        str_add(out, u);
        return 0;
    }
    slen = (size_t)(sep - u);
    for (k = 0U; k < (sizeof ssh / sizeof ssh[0]); k++) {
        if ((strlen(ssh[k]) == slen) && (strncmp(u, ssh[k], slen) == 0)) {
            is_ssh = 1;
        }
    }
    str_addn(out, u, (size_t)(auth - u));
    if (is_ssh != 0) {
        colon = memchr(auth, ':', (size_t)(at - auth));
        str_addn(out, auth, (size_t)(((colon != NULL) ? colon : at) - auth));
        str_add(out, at);
        return colon != NULL;
    }
    str_add(out, at + 1);
    return 1;
}

/* Credenziali tolte dall'URL di un remote: un avviso per remote e per
 * processo, senza l'URL (lo stamperebbe). */
static void warn_credentials(const char *name)
{
    static Str_t warned = STR_INIT;

    if (!str_zhas(&warned, name)) {
        str_addz(&warned, name);
        msg_warning(MSG_WARN_URL_CREDENTIALS, name);
    }
}

/* Primo valore di remote.<name>.url, come e' scritto nella configurazione
 * (il primo e' quello che git usa per il fetch). 1 se c'e'. */
static int config_url(const char *name, Str_t *url)
{
    const char *args[] = { "config", "--get-all", NULL, NULL };
    Str_t key = STR_INIT;
    const char *nl;
    int ok;

    str_reset(url);
    str_addf(&key, "remote.%s.url", name);
    args[2] = str_cstr(&key);
    ok = (git_run(args, PROC_STDERR_NULL, url) == GS_OK) && (url->len > 0U);
    nl = ok ? strchr(str_cstr(url), '\n') : NULL;
    if (nl != NULL) {
        str_setlen(url, (size_t)(nl - url->buf));
    }
    str_free(&key);
    return ok && (url->len > 0U);
}

eGsErr_t remote_url(const char *top, const char *name, Str_t *url)
{
    const char *args[] = { "remote", "get-url", "--", name, NULL };
    Str_t abs = STR_INIT;
    Str_t raw = STR_INIT;
    eGsErr_t err;

    str_reset(url);
    err = git_run(args, PROC_STDERR_NULL, url);
    /* AB-02: get-url espande url.<base>.insteadOf, una preferenza locale
     * (ssh al posto di https, un mirror interno) che nella catena varrebbe
     * per tutti. Si tiene l'URL configurato se e' completo (schema://, o un
     * path); un'abbreviazione (gh:luca/A) resta espansa. */
    if ((err == GS_OK) && config_url(name, &raw) &&
        ((strstr(str_cstr(&raw), "://") != NULL) || url_is_path(str_cstr(&raw)))) {
        str_reset(url);
        str_add(url, str_cstr(&raw));
    }
    if ((err == GS_OK) && url_public(str_cstr(url), &raw)) {
        warn_credentials(name);
        str_reset(url);
        str_add(url, str_cstr(&raw));
    }
    str_free(&raw);
    if ((err == GS_OK) && url_is_path(str_cstr(url)) &&
        !path_is_absolute(str_cstr(url))) {
        /* P-08: git risolve un path relativo dalla radice della repo */
        if (join_abs(top, str_cstr(url), &abs)) {
            str_reset(url);
            str_add(url, str_cstr(&abs));
        } else {
            err = GS_ERR_FORMAT;
        }
    }
    if ((err == GS_OK) && !remote_url_ok(str_cstr(url))) {
        err = GS_ERR_FORMAT;
    }
    if (err != GS_OK) {
        str_reset(url);
    }
    str_free(&abs);
    return err;
}

eGsErr_t remote_arg_url(const char *top, const char *cwd, const char *arg,
                        Str_t *url)
{
    Str_t abs = STR_INIT;
    eGsErr_t err = GS_OK;

    if (remote_url(top, arg, url) == GS_OK) {
        return GS_OK; /* un remote con quel nome */
    }
    str_reset(url);
    if (url_is_path(arg) && (strchr(arg, '/') == NULL) && (strchr(arg, '\\') == NULL)) {
        err = GS_ERR_FORMAT; /* una parola: un remote che non c'e' ("./x" per
                              * una cartella) */
    } else if (url_is_path(arg) && !path_is_absolute(arg)) {
        if (join_abs(cwd, arg, &abs)) {
            str_add(url, str_cstr(&abs));
        } else {
            err = GS_ERR_FORMAT;
        }
    } else if (url_public(arg, &abs)) {
        /* scritto dall'utente con le credenziali: nella catena no (S-01) */
        msg_warning(MSG_WARN_URL_ARG_CREDENTIALS);
        str_add(url, str_cstr(&abs));
    } else {
        str_add(url, arg);
    }
    if ((err == GS_OK) && !remote_url_ok(str_cstr(url))) {
        err = GS_ERR_FORMAT;
    }
    if (err != GS_OK) {
        str_reset(url);
    }
    str_free(&abs);
    return err;
}

/* Toglie da key i "/" finali e, se strip_git, un ".git" finale. */
static void trim_tail(Str_t *key, int strip_git)
{
    while ((key->len > 1U) && (key->buf[key->len - 1U] == '/')) {
        str_setlen(key, key->len - 1U);
    }
    if (strip_git && (key->len > 4U) &&
        (strcmp(key->buf + key->len - 4U, ".git") == 0)) {
        str_setlen(key, key->len - 4U);
        trim_tail(key, 0);
    }
}

/* Aggiunge p a key: separatori in '/', doppi ridotti a uno; minuscolo se
 * lower. */
static void add_path(Str_t *key, const char *p, int lower)
{
    for (; *p != '\0'; p++) {
        if (is_sep(*p)) {
            if ((key->len == 0U) || (key->buf[key->len - 1U] != '/')) {
                str_addch(key, '/');
            }
        } else {
            str_addch(key, lower ? (char)tolower((unsigned char)*p) : *p);
        }
    }
}

/* Le parti di un URL di rete: scheme://[utente@]host[:porta]/path oppure
 * [utente@]host:path (forma scp, scheme_len = 0). host con le [] di un
 * indirizzo IPv6. */
typedef struct {
    const char *scheme;
    size_t scheme_len;
    const char *user;
    size_t user_len;
    const char *host;
    size_t host_len;
    const char *port;
    size_t port_len;
    const char *path; /* URL: dal '/' dopo l'host; scp: dopo il ':' */
} UrlParts_t;

static void url_parts(const char *u, UrlParts_t *p)
{
    const char *sep = strstr(u, "://");
    const char *auth = (sep != NULL) ? (sep + 3) : u;
    const char *end = NULL;
    const char *at = NULL;
    const char *h;
    const char *hend;
    const char *c;
    int bracket = 0;

    memset(p, 0, sizeof *p);
    p->scheme = u;
    p->scheme_len = (sep != NULL) ? (size_t)(sep - u) : 0U;
    if (sep != NULL) {
        end = auth + strcspn(auth, "/");
    } else {
        /* scp: l'host finisce al primo ':' fuori dalle [] di IPv6 */
        for (c = auth; (*c != '\0') && (end == NULL); c++) {
            if (*c == '[') {
                bracket = 1;
            } else if (*c == ']') {
                bracket = 0;
            } else if ((*c == ':') && (bracket == 0)) {
                end = c;
            } else {
                /* parte dell'host */
            }
        }
        end = (end != NULL) ? end : (auth + strlen(auth));
    }
    for (c = auth; c < end; c++) {
        if (*c == '@') {
            at = c; /* l'ultima */
        }
    }
    h = (at != NULL) ? (at + 1) : auth;
    if (at != NULL) {
        p->user = auth;
        p->user_len = (size_t)(at - auth);
    }
    /* B-SA1: la porta si cerca dopo la ']' di un indirizzo IPv6 */
    hend = NULL;
    if (*h == '[') {
        hend = memchr(h, ']', (size_t)(end - h));
        hend = (hend != NULL) ? (hend + 1) : NULL;
    }
    if (hend == NULL) {
        hend = (sep != NULL) ? memchr(h, ':', (size_t)(end - h)) : NULL;
        hend = (hend != NULL) ? hend : end;
    }
    p->host = h;
    p->host_len = (size_t)(hend - h);
    if ((sep != NULL) && (hend < end) && (*hend == ':')) {
        p->port = hend + 1;
        p->port_len = (size_t)(end - (hend + 1));
    }
    p->path = (sep != NULL) ? end : ((*end == ':') ? (end + 1) : end);
}

/* 1 se n byte di s sono uguali a w, senza distinzione di maiuscole. */
static int word_is(const char *s, size_t n, const char *w)
{
    size_t k;

    if (strlen(w) != n) {
        return 0;
    }
    for (k = 0U; k < n; k++) {
        if (tolower((unsigned char)s[k]) != (unsigned char)w[k]) {
            return 0;
        }
    }
    return 1;
}

/*
 * Hosting noti, dove path e nome della repo non distinguono le maiuscole,
 * ".git" finale e utente non cambiano la repo, e https e ssh portano alla
 * stessa repo (S-06).
 */
static int host_known(const UrlParts_t *p)
{
    static const char *const known[] = { "github.com", "ssh.github.com",
                                         "gitlab.com", "bitbucket.org",
                                         "codeberg.org" };
    size_t k;

    for (k = 0U; k < (sizeof known / sizeof known[0]); k++) {
        if (word_is(p->host, p->host_len, known[k])) {
            return 1;
        }
    }
    return 0;
}

/* Classe del trasporto per la forma stretta: scp e git+ssh sono ssh. */
static void add_scheme(Str_t *key, const UrlParts_t *p)
{
    size_t k;

    if ((p->scheme_len == 0U) || word_is(p->scheme, p->scheme_len, "git+ssh") ||
        word_is(p->scheme, p->scheme_len, "ssh+git")) {
        str_add(key, "ssh");
        return;
    }
    for (k = 0U; k < p->scheme_len; k++) {
        str_addch(key, (char)tolower((unsigned char)p->scheme[k]));
    }
}

/* 1 se la porta e' quella di default del trasporto (o non c'e'). */
static int port_default(const UrlParts_t *p)
{
    static const struct {
        const char *scheme;
        const char *port;
    } def[] = { { "ssh", "22" }, { "git+ssh", "22" }, { "ssh+git", "22" },
                { "https", "443" }, { "http", "80" }, { "git", "9418" } };
    size_t k;

    if (p->port_len == 0U) {
        return 1;
    }
    for (k = 0U; k < (sizeof def / sizeof def[0]); k++) {
        if (word_is(p->scheme, p->scheme_len, def[k].scheme) &&
            (strlen(def[k].port) == p->port_len) &&
            (strncmp(p->port, def[k].port, p->port_len) == 0)) {
            return 1;
        }
    }
    return 0;
}

/*
 * Forma normale di un URL. Larga (strict = 0, remote_same): host senza
 * utente ne' porta, path in minuscolo e senza ".git". Stretta (strict = 1,
 * remote_same_strict): per gli hosting noti come la larga; per gli altri
 * restano il trasporto, l'utente di ssh, una porta che non e' quella di
 * default, maiuscole e ".git" del path, e un path scp relativo resta
 * relativo alla home ("/~/"). I path locali sono uguali nelle due forme.
 */
static void url_key(const char *u, Str_t *key, int strict)
{
    UrlParts_t p;
    size_t k;
    int lower_path = 0;

    str_reset(key);
#ifdef _WIN32
    lower_path = 1; /* file system senza distinzione di maiuscole */
#endif
    if (strncmp(u, "file://", 7U) == 0) {
        u += 7;
        if ((u[0] == '/') && is_drive(u + 1)) {
            u++; /* file:///C:/x */
        }
    }
    if (url_is_path(u)) {
        str_add(key, "path:");
        add_path(key, u, lower_path);
        trim_tail(key, 0);
        return;
    }
    url_parts(u, &p);
    str_add(key, "net:");
    if ((strict == 0) || host_known(&p)) {
        for (k = 0U; k < p.host_len; k++) {
            str_addch(key, (char)tolower((unsigned char)p.host[k]));
        }
        str_addch(key, '/');
        add_path(key, p.path, 1);
        trim_tail(key, 1);
        return;
    }
    add_scheme(key, &p);
    str_add(key, "://");
    if ((p.user_len > 0U) && (strncmp(str_cstr(key) + 4, "ssh:", 4U) == 0)) {
        str_addn(key, p.user, p.user_len);
        str_addch(key, '@');
    }
    for (k = 0U; k < p.host_len; k++) {
        str_addch(key, (char)tolower((unsigned char)p.host[k]));
    }
    if (!port_default(&p)) {
        str_addch(key, ':');
        str_addn(key, p.port, p.port_len);
    }
    str_addch(key, '/');
    if ((p.scheme_len == 0U) && (p.path[0] != '/') && (p.path[0] != '\0')) {
        str_add(key, "~/"); /* scp: relativo alla home */
    }
    add_path(key, p.path, 0);
    trim_tail(key, 0);
}

static int same_key(const char *a, const char *b, int strict)
{
    Str_t ka = STR_INIT;
    Str_t kb = STR_INIT;
    int same;

    url_key(a, &ka, strict);
    url_key(b, &kb, strict);
    same = str_eq(&ka, str_cstr(&kb));
    str_free(&kb);
    str_free(&ka);
    return same;
}

int remote_same(const char *a, const char *b)
{
    return same_key(a, b, 0);
}

int remote_same_strict(const char *a, const char *b)
{
    return same_key(a, b, 1);
}

/* La casata: la forma normale senza l'ultimo componente del path. */
static void house_key(const char *u, Str_t *key)
{
    const char *slash;

    url_key(u, key, 0);
    slash = strrchr(str_cstr(key), '/');
    if (slash != NULL) {
        str_setlen(key, (size_t)(slash - key->buf));
    }
}

int remote_same_house(const char *a, const char *b)
{
    Str_t ka = STR_INIT;
    Str_t kb = STR_INIT;
    int same;

    house_key(a, &ka);
    house_key(b, &kb);
    same = str_eq(&ka, str_cstr(&kb));
    str_free(&kb);
    str_free(&ka);
    return same;
}

/* 1 se l'elenco di coppie nome, URL ha gia' il nome. */
static int remote_pair_name(const Str_t *pairs, const char *name)
{
    const char *p;

    for (p = str_znext(pairs, NULL); p != NULL; p = str_znext(pairs, p)) {
        if (strcmp(p, name) == 0) {
            return 1;
        }
        p = str_znext(pairs, p); /* l'URL */
        if (p == NULL) {
            break;
        }
    }
    return 0;
}

/*
 * Remote configurati della repo: elenco compatto di coppie nome, URL (il
 * primo remote.<name>.url, un path relativo reso assoluto come in
 * remote_url). Letti una volta per processo, con un solo git config.
 */
static const Str_t *configured_remotes(void)
{
    static const char *const args[] = { "config", "--get-regexp",
                                        "^remote\\..*\\.url$", NULL };
    static const char *const top_args[] = { "rev-parse", "--show-toplevel", NULL };
    static Str_t pairs = STR_INIT;
    static int loaded = 0;
    static const char key_frame[] = "remote..url"; /* "remote." + nome + ".url" */
    Str_t out = STR_INIT;
    Str_t top = STR_INIT;
    Str_t name = STR_INIT;
    Str_t value = STR_INIT;
    Str_t joined = STR_INIT;
    const char *line;
    const char *nl;
    const char *sp;
    size_t klen;
    int have_top = 0;

    if (loaded != 0) {
        return &pairs;
    }
    loaded = 1;
    if (git_run(args, PROC_STDERR_NULL, &out) != GS_OK) {
        str_reset(&out);
    }
    /* Righe "remote.<nome>.url <valore>": il nome puo' contenere punti. */
    for (line = str_cstr(&out); *line != '\0'; line = (*nl == '\n') ? (nl + 1) : nl) {
        nl = strchr(line, '\n');
        nl = (nl != NULL) ? nl : (line + strlen(line));
        sp = memchr(line, ' ', (size_t)(nl - line));
        klen = (sp != NULL) ? (size_t)(sp - line) : 0U;
        if ((klen <= (sizeof key_frame - 1U)) || ((sp + 1) >= nl)) {
            continue;
        }
        str_reset(&name);
        str_addn(&name, line + 7, klen - (sizeof key_frame - 1U));
        str_reset(&value);
        str_addn(&value, sp + 1, (size_t)(nl - (sp + 1)));
        if (remote_pair_name(&pairs, str_cstr(&name))) {
            continue; /* un URL in piu' dello stesso remote: vale il primo */
        }
        if (url_is_path(str_cstr(&value)) && !path_is_absolute(str_cstr(&value))) {
            if (have_top == 0) {
                have_top = (git_run(top_args, PROC_STDERR_NULL, &top) == GS_OK) ? 1 : -1;
            }
            if ((have_top < 0) || !join_abs(str_cstr(&top), str_cstr(&value), &joined)) {
                continue;
            }
            str_reset(&value);
            str_add(&value, str_cstr(&joined));
        }
        str_addz(&pairs, str_cstr(&name));
        str_addz(&pairs, str_cstr(&value));
    }
    str_free(&joined);
    str_free(&value);
    str_free(&name);
    str_free(&top);
    str_free(&out);
    return &pairs;
}

/*
 * Come si raggiunge url (AB-03): se e' l'URL di un remote configurato (in
 * forma normale, quindi anche senza le credenziali che remote_url toglie),
 * il nome di quel remote. Cosi' git usa il suo pushurl, insteadOf,
 * pushInsteadOf e le credenziali scritte nell'URL configurato, come in un
 * "git push <remote>". Altrimenti url stesso. Un nome che comincia con '-'
 * diventerebbe un'opzione: si usa l'URL.
 */
static const char *transport(const char *url)
{
    const Str_t *pairs = configured_remotes();
    const char *name;
    const char *value;

    for (name = str_znext(pairs, NULL); name != NULL; name = str_znext(pairs, value)) {
        value = str_znext(pairs, name);
        if (value == NULL) {
            break;
        }
        if ((name[0] != '-') && remote_same(value, url)) {
            return name;
        }
    }
    return url;
}

eGsErr_t remote_creator_url(GitBatch_t *b, const Oid_t *version, Str_t *url)
{
    Auth_t a = AUTH_INIT;
    AuthBlobs_t ab;
    eGsErr_t err;

    str_reset(url);
    auth_blobs(b, version, &ab);
    ab.sovereigns.len = 0U; /* solo il creatore */
    err = auth_read(b, &ab, &a);
    if (err == GS_OK) {
        str_add(url, str_cstr(&a.creator));
    }
    auth_free(&a);
    return err;
}

eGsErr_t remote_url_blob(const char *url, Oid_t *blob)
{
    static const char *const args[] = { "hash-object", "-w", "--stdin", NULL };
    Str_t out = STR_INIT;
    Proc_t p;
    int code = 0;
    eGsErr_t err;

    if (!remote_url_ok(url)) {
        return GS_ERR_FORMAT;
    }
    err = git_start(&p, args, PROC_STDIN_PIPE);
    if (err != GS_OK) {
        goto exit;
    }
    if (fprintf(p.in, "%s\n", url) < 0) {
        err = GS_ERR_GIT;
    }
    proc_close_in(&p);
    if (proc_collect(&p, &out, &code) != GS_OK) {
        err = GS_ERR_PROC;
    }
    str_trim_eol(&out);
    if ((err == GS_OK) && ((code != 0) || !oid_from_hex(blob, out.buf, out.len))) {
        err = GS_ERR_GIT;
    }

exit:
    str_free(&out);
    return err;
}

/*
 * Un fetch delle catene dell'elenco nelle ref dell'ultima versione vista,
 * con i refspec sullo stdin (fetch --stdin, git >= 2.29): nessun limite
 * della riga di comando. sel = NULL: tutti gli ID; altrimenti quelli con
 * sel[k] != 0. 1 se riesce.
 */
static int fetch_specs(const char *url, const Str_t *ids, const uint8_t *sel)
{
    const char *args[NET_OPTS_N + 10U];
    Str_t out = STR_INIT;
    Proc_t p;
    const char *id;
    size_t k;
    size_t na = add_net_opts(args);
    int code = -1;
    int ok = 0;

    args[na++] = "fetch";
    args[na++] = "-q";
    args[na++] = "--no-tags";
    args[na++] = "--no-write-fetch-head";
    args[na++] = "--no-auto-gc";
    args[na++] = "--no-recurse-submodules";
    args[na++] = "--stdin";
    args[na++] = transport(url);
    args[na] = NULL;
    no_prompt();
    if (git_start(&p, args, PROC_STDIN_PIPE | PROC_STDERR_NULL) != GS_OK) {
        goto exit;
    }
    for (k = 0U, id = str_znext(ids, NULL); id != NULL;
         k++, id = str_znext(ids, id)) {
        if ((sel == NULL) || (sel[k] != 0U)) {
            (void)fprintf(p.in, "+refs/shared/%s:refs/shared-authority/%s\n",
                          id, id);
        }
    }
    proc_close_in(&p);
    ok = (proc_collect(&p, &out, &code) == GS_OK) && (code == 0);

exit:
    str_free(&out);
    return ok;
}

/*
 * Quali ID dell'elenco ha la repo remota: has[k] (ls-remote delle sole
 * refs/shared/). 0 se la repo non si raggiunge.
 */
static int remote_has(const char *url, const Str_t *ids, uint8_t *has)
{
    static const char prefix[] = "refs/shared/";
    const char *args[NET_OPTS_N + 4U];
    Str_t out = STR_INIT;
    const char *line;
    const char *tab;
    const char *nl;
    const char *id;
    size_t len;
    size_t k;
    int ok;

    k = add_net_opts(args);
    args[k++] = "ls-remote";
    args[k++] = transport(url);
    args[k++] = "refs/shared/*";
    args[k] = NULL;
    for (k = 0U, id = str_znext(ids, NULL); id != NULL;
         k++, id = str_znext(ids, id)) {
        has[k] = 0U;
    }
    no_prompt();
    ok = (git_run(args, PROC_STDERR_NULL, &out) == GS_OK);
    /* Righe "<oid>\t<ref>": il pattern di ls-remote confronta la parte
     * finale del nome, quindi si cerca la ref esatta. */
    for (line = str_cstr(&out); ok && (*line != '\0');
         line = (*nl == '\n') ? (nl + 1) : nl) {
        nl = strchr(line, '\n');
        nl = (nl != NULL) ? nl : (line + strlen(line));
        tab = memchr(line, '\t', (size_t)(nl - line));
        if ((tab == NULL) ||
            (strncmp(tab + 1, prefix, sizeof prefix - 1U) != 0)) {
            continue;
        }
        tab += sizeof prefix; /* il nome dopo refs/shared/ */
        len = (size_t)(nl - tab);
        for (k = 0U, id = str_znext(ids, NULL); id != NULL;
             k++, id = str_znext(ids, id)) {
            if ((strlen(id) == len) && (strncmp(id, tab, len) == 0)) {
                has[k] = 1U;
            }
        }
    }
    str_free(&out);
    return ok;
}

int remote_reachable(const char *url)
{
    Str_t none = STR_INIT;
    uint8_t has = 0U;
    int ok;

    ok = remote_url_ok(url) && remote_has(url, &none, &has);
    str_free(&none);
    return ok;
}

void remote_fetch_seen(const char *url, const Str_t *ids, uint8_t *res)
{
    size_t n = str_zcount(ids);
    uint8_t *has = xmalloc((n > 0U) ? n : 1U);
    size_t k;
    int any = 0;

    for (k = 0U; k < n; k++) {
        res[k] = (uint8_t)REMOTE_UNREACHABLE;
    }
    if ((n == 0U) || !remote_url_ok(url)) {
        goto exit;
    }
    if (fetch_specs(url, ids, NULL)) {
        for (k = 0U; k < n; k++) {
            res[k] = (uint8_t)REMOTE_GOT;
        }
        goto exit;
    }
    /* Il fetch fallisce tutto se manca anche una sola ref: si chiede quali
     * ci sono e si riprova con quelle. */
    if (!remote_has(url, ids, has)) {
        goto exit;
    }
    for (k = 0U; k < n; k++) {
        if (has[k] == 0U) {
            res[k] = (uint8_t)REMOTE_MISSING;
        } else {
            any = 1;
        }
    }
    if ((any != 0) && fetch_specs(url, ids, has)) {
        for (k = 0U; k < n; k++) {
            if (has[k] != 0U) {
                res[k] = (uint8_t)REMOTE_GOT;
            }
        }
    }

exit:
    free(has);
}

/* Un push di n versioni; idv[k] = ID della versione v[k]. */
static void push_chunk(const char *url, const char *const *idv, const Oid_t *v,
                       size_t n, uint8_t *res)
{
    /* --no-verify: non e' un push dell'utente, il suo pre-push non gira
     * (D-080). Senza "+": un push che non e' fast-forward lo rifiuta git. */
    const char **args = xmalloc((n + NET_OPTS_N + 5U) * sizeof *args);
    Str_t specs = STR_INIT;
    Str_t one = STR_INIT;
    Str_t out = STR_INIT;
    char hex[OID_HEX_SIZE];
    const char *s;
    char *line;
    char *nl;
    size_t na = 0U;
    size_t k;
    int code = -1;

    for (k = 0U; k < n; k++) {
        res[k] = (uint8_t)REMOTE_PUSH_REFUSED;
    }
    if (!remote_url_ok(url)) {
        goto exit;
    }
    na = add_net_opts(args);
    args[na++] = "push";
    args[na++] = "--porcelain";
    args[na++] = "--no-verify";
    args[na++] = transport(url); /* AB-03 */
    for (k = 0U; k < n; k++) {
        oid_to_hex(&v[k], hex);
        str_reset(&one);
        str_addf(&one, "%s:refs/shared/%s", hex, idv[k]);
        str_addz(&specs, str_cstr(&one));
    }
    for (s = str_znext(&specs, NULL); s != NULL; s = str_znext(&specs, s)) {
        args[na++] = s;
    }
    args[na] = NULL;
    no_prompt();
    if (git_capture(args, PROC_STDERR_NULL, &out, &code) != GS_OK) {
        goto exit;
    }
    /* --porcelain: una riga per ref, "<flag>\t<da>:<a>\t<esito>"; "!" se
     * rifiutata. L'esito dice chi l'ha rifiutata (A-28): "[rejected]" il
     * client, perche' la repo ha una versione che qui manca; "[remote
     * rejected]" il server. Nessuna riga: repo irraggiungibile. */
    for (line = out.buf; (line != NULL) && (*line != '\0');
         line = (nl != NULL) ? (nl + 1) : NULL) {
        nl = strchr(line, '\n');
        if (nl != NULL) {
            *nl = '\0';
        }
        for (k = 0U; k < n; k++) {
            str_reset(&one);
            str_addf(&one, ":refs/shared/%s\t", idv[k]);
            if (strstr(line, str_cstr(&one)) == NULL) {
                continue;
            }
            if (line[0] != '!') {
                res[k] = (uint8_t)REMOTE_PUSH_OK;
            } else if (strstr(line, "\t[rejected]") != NULL) {
                res[k] = (uint8_t)REMOTE_PUSH_NEWER;
            } else {
                /* [remote rejected], permessi: la versione resta qui */
            }
        }
    }

exit:
    free((void *)args);
    str_free(&out);
    str_free(&one);
    str_free(&specs);
}

void remote_push_many(const char *url, const Str_t *ids, const Oid_t *v,
                      uint8_t *res)
{
    const char *idv[REMOTE_PUSH_CHUNK];
    const char *id;
    size_t start = 0U;
    size_t k = 0U;

    for (id = str_znext(ids, NULL); id != NULL; id = str_znext(ids, id)) {
        idv[k - start] = id;
        k++;
        if ((k - start) == REMOTE_PUSH_CHUNK) {
            push_chunk(url, idv, &v[start], k - start, &res[start]);
            start = k;
        }
    }
    if (k > start) {
        push_chunk(url, idv, &v[start], k - start, &res[start]);
    }
}

eRemotePush_t remote_push(const char *url, const char *id, const Oid_t *v)
{
    uint8_t res = (uint8_t)REMOTE_PUSH_REFUSED;

    push_chunk(url, &id, v, 1U, &res);
    return (eRemotePush_t)res;
}

void remote_seen_ref(Str_t *ref, const char *id)
{
    str_reset(ref);
    str_addf(ref, "refs/shared-authority/%s", id);
}

eGsErr_t remote_seen_write(const char *id, const char *url, Str_t *mem_url,
                           unsigned long *mem_at)
{
    const char *args[] = { "config", NULL, NULL, NULL };
    Str_t key = STR_INIT;
    Str_t value = STR_INIT;
    time_t now = time(NULL);
    unsigned long at = (now > (time_t)0) ? (unsigned long)now : 0UL;
    eGsErr_t err;

    str_addf(&key, "shared.%s.authoritySeen", id);
    str_addf(&value, "%lu %s", at, url);
    args[1] = str_cstr(&key);
    args[2] = str_cstr(&value);
    err = git_run(args, 0U, NULL);
    if ((err == GS_OK) && (mem_url != NULL)) {
        str_reset(mem_url);
        str_add(mem_url, url);
    }
    if ((err == GS_OK) && (mem_at != NULL)) {
        *mem_at = at;
    }
    str_free(&value);
    str_free(&key);
    return err;
}

int remote_seen_parse(const char *value, Str_t *url, unsigned long *at)
{
    char *end = NULL;
    unsigned long v;

    str_reset(url);
    *at = 0UL;
    if (!isdigit((unsigned char)value[0])) {
        return 0;
    }
    v = strtoul(value, &end, 10);
    if ((end == NULL) || (*end != ' ') || !remote_url_ok(end + 1)) {
        return 0;
    }
    str_add(url, end + 1);
    *at = v;
    return 1;
}

int remote_find_own(GitBatch_t *b, const char *url, const char *id,
                    const Oid_t *t, const Oid_t *p2, const AuthBlobs_t *auth,
                    Oid_t *v)
{
    const char *rp[] = { "rev-parse", "--verify", "-q", NULL, NULL };
    char hex[OID_HEX_SIZE];
    Str_t tmp = STR_INIT;
    Str_t del = STR_INIT;
    Str_t name = STR_INIT;
    Str_t out = STR_INIT;
    AuthBlobs_t got;
    Oid_t tip;
    Oid_t oid;
    Oid_t mine;
    int found = 0;

    /* Ref temporanea di questo processo, cancellata subito (B-SA3). */
    peers_tmp_ref(&tmp, id);
    if (remote_fetch(url, id, str_cstr(&tmp)) != GS_OK) {
        goto exit;
    }
    rp[3] = str_cstr(&tmp);
    if ((git_run(rp, PROC_STDERR_NULL, &out) != GS_OK) ||
        !oid_from_hex(&tip, out.buf, out.len)) {
        goto exit;
    }
    oid_to_hex(&tip, hex);
    /* i genitori: T, e p2 se c'e' */
    str_addf(&name, "%s^1", hex);
    if ((git_batch_query(b, str_cstr(&name), &oid) != GIT_OBJ_COMMIT) ||
        !oid_eq(&oid, t)) {
        goto exit;
    }
    str_reset(&name);
    str_addf(&name, "%s^2", hex);
    if ((p2 == NULL) ? (git_batch_query(b, str_cstr(&name), &oid) != GIT_OBJ_MISSING)
                     : ((git_batch_query(b, str_cstr(&name), &oid) != GIT_OBJ_COMMIT) ||
                        !oid_eq(&oid, p2))) {
        goto exit;
    }
    /* lo stesso contenuto di T */
    str_reset(&name);
    str_addf(&name, "%s:content", hex);
    if (!git_batch_blob(b, str_cstr(&name), &oid)) {
        goto exit;
    }
    oid_to_hex(t, hex);
    str_reset(&name);
    str_addf(&name, "%s:content", hex);
    if (!git_batch_blob(b, str_cstr(&name), &mine) || !oid_eq(&oid, &mine)) {
        goto exit;
    }
    /* le voci creator, sovereigns e heirs che questo push scriverebbe */
    auth_blobs(b, &tip, &got);
    if ((got.creator.len == 0U) || !auth_blobs_eq_all(&got, auth)) {
        goto exit;
    }
    *v = tip;
    found = 1;

exit:
    str_addz(&del, str_cstr(&tmp));
    (void)git_delete_refs(&del);
    str_free(&out);
    str_free(&name);
    str_free(&del);
    str_free(&tmp);
    return found;
}

eGsErr_t remote_fetch(const char *url, const char *id, const char *ref)
{
    const char *args[NET_OPTS_N + 10U];
    Str_t spec = STR_INIT;
    size_t na = add_net_opts(args);
    eGsErr_t err = GS_ERR_GIT;

    if (remote_url_ok(url)) {
        no_prompt();
        str_addf(&spec, "+refs/shared/%s:%s", id, ref);
        args[na++] = "fetch";
        args[na++] = "-q";
        args[na++] = "--no-tags";
        args[na++] = "--no-write-fetch-head";
        args[na++] = "--no-auto-gc";
        args[na++] = "--no-recurse-submodules";
        args[na++] = transport(url);
        args[na++] = str_cstr(&spec);
        args[na] = NULL;
        err = git_run(args, PROC_STDERR_NULL, NULL);
    }
    str_free(&spec);
    return err;
}
