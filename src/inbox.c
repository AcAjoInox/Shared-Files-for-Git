/*
 * inbox.c - avvisi di nuove versioni tra repo locali (D-099).
 */
#include "inbox.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bounds.h"
#include "msg.h"
#include "sys.h"

void inbox_free(Inbox_t *in)
{
    oidlist_free(&in->oids);
    str_free(&in->from);
}

int inbox_root(const char *top, Str_t *dir)
{
    int ok;

    str_reset(dir);
    str_addf(dir, "%s/.git", top);
    ok = sys_is_dir(str_cstr(dir));
    if (ok) {
        str_add(dir, "/shared/inbox");
    }
    return ok;
}

/* <top>/.git/shared/inbox/<id>; 0 se la repo non riceve avvisi. */
static int id_dir(const char *top, const char *id, Str_t *dir)
{
    int ok = inbox_root(top, dir);

    if (ok) {
        str_addf(dir, "/%s", id);
    }
    return ok;
}

/* Prima riga di un file piccolo, senza fine riga; "" se non si legge. */
static void read_line(const char *path, Str_t *out)
{
    char buf[GS_PATH_MAX + 2U];
    FILE *f = sys_fopen(path, "rb");
    size_t n = 0U;

    str_reset(out);
    if (f != NULL) {
        n = fread(buf, 1U, sizeof buf - 1U, f);
        (void)fclose(f);
    }
    buf[n] = '\0';
    buf[strcspn(buf, "\r\n")] = '\0';
    if (strlen(buf) <= GS_PATH_MAX) { /* troppo lungo: repo sconosciuta */
        str_add(out, buf);
    }
}

void inbox_read(const Repo_t *r, const char *id, Inbox_t *in)
{
    size_t hexlen = str_eq(&r->objfmt, "sha256") ? 64U : 40U;
    Str_t dir = STR_INIT;
    Str_t names = STR_INIT;
    Str_t file = STR_INIT;
    Str_t from = STR_INIT;
    const char *n;
    Oid_t oid;

    if (!id_dir(str_cstr(&r->top), id, &dir) ||
        (sys_list_dir(str_cstr(&dir), &names) != GS_OK)) {
        goto exit;
    }
    for (n = str_znext(&names, NULL); n != NULL; n = str_znext(&names, n)) {
        if ((strlen(n) == hexlen) && oid_from_hex(&oid, n, hexlen) &&
            !oidlist_has(&in->oids, &oid)) {
            str_reset(&file);
            str_addf(&file, "%s/%s", str_cstr(&dir), n);
            read_line(str_cstr(&file), &from);
            oidlist_add(&in->oids, &oid);
            str_addz(&in->from, str_cstr(&from));
        }
    }

exit:
    str_free(&from);
    str_free(&file);
    str_free(&names);
    str_free(&dir);
}

void inbox_drop(const Repo_t *r, const char *id, const Oid_t *oid)
{
    char hex[OID_HEX_SIZE];
    Str_t file = STR_INIT;

    if (id_dir(str_cstr(&r->top), id, &file)) {
        oid_to_hex(oid, hex);
        str_addf(&file, "/%s", hex);
        (void)sys_remove(str_cstr(&file));
    }
    str_free(&file);
}

void inbox_remove(const char *top, const char *id)
{
    Str_t dir = STR_INIT;
    Str_t names = STR_INIT;
    Str_t file = STR_INIT;
    const char *n;

    if (id_dir(top, id, &dir) && (sys_list_dir(str_cstr(&dir), &names) == GS_OK)) {
        for (n = str_znext(&names, NULL); n != NULL; n = str_znext(&names, n)) {
            str_reset(&file);
            str_addf(&file, "%s/%s", str_cstr(&dir), n);
            (void)sys_remove(str_cstr(&file));
        }
        (void)sys_rmdir(str_cstr(&dir));
    }
    str_free(&file);
    str_free(&names);
    str_free(&dir);
}

void inbox_withdraw(const char *top, const char *id, const Str_t *from)
{
    Str_t dir = STR_INIT;
    Str_t names = STR_INIT;
    Str_t file = STR_INIT;
    Str_t who = STR_INIT;
    const char *n;

    if (id_dir(top, id, &dir) && (sys_list_dir(str_cstr(&dir), &names) == GS_OK)) {
        for (n = str_znext(&names, NULL); n != NULL; n = str_znext(&names, n)) {
            str_reset(&file);
            str_addf(&file, "%s/%s", str_cstr(&dir), n);
            read_line(str_cstr(&file), &who);
            if ((who.len > 0U) && str_zhas(from, str_cstr(&who))) {
                (void)sys_remove(str_cstr(&file));
            }
        }
    }
    str_free(&who);
    str_free(&file);
    str_free(&names);
    str_free(&dir);
}

void inbox_ids(const char *top, Str_t *ids)
{
    Str_t dir = STR_INIT;

    if (inbox_root(top, &dir)) {
        (void)sys_list_dir(str_cstr(&dir), ids);
    }
    str_free(&dir);
}

int inbox_has(const char *peer, const char *id, const Oid_t *oid)
{
    char hex[OID_HEX_SIZE];
    Str_t file = STR_INIT;
    FILE *f = NULL;

    if (id_dir(peer, id, &file)) {
        oid_to_hex(oid, hex);
        str_addf(&file, "/%s", hex);
        f = sys_fopen(str_cstr(&file), "rb");
    }
    if (f != NULL) {
        (void)fclose(f);
    }
    str_free(&file);
    return f != NULL;
}

/* Crea <peer>/.git/shared/inbox/<id> livello per livello. 0 = c'e'. */
static int make_dirs(const char *peer, const char *id, Str_t *dir)
{
    static const char *const level[] = { "/shared", "/inbox", NULL };
    size_t i;
    int rc = 0;

    str_reset(dir);
    str_addf(dir, "%s/.git", peer);
    for (i = 0U; (level[i] != NULL) && (rc == 0); i++) {
        str_add(dir, level[i]);
        rc = sys_mkdir(str_cstr(dir));
    }
    if (rc == 0) {
        str_addf(dir, "/%s", id);
        rc = sys_mkdir(str_cstr(dir));
    }
    return rc;
}

/* Scrive l'avviso nella cartella data. 0 = c'e' (scritto ora o prima). */
static int write_notice(const char *dir, const char *hex, const char *self)
{
    Str_t file = STR_INIT;
    Str_t tmp = STR_INIT;
    uint8_t raw[4];
    FILE *f;
    int put_rc;
    int close_rc;
    int rc = -1;

    str_addf(&file, "%s/%s", dir, hex);
    f = sys_fopen(str_cstr(&file), "rb");
    if (f != NULL) {
        (void)fclose(f);
        rc = 0;
        goto exit;
    }
    /* Nome temporaneo unico: con un punto, non e' mai un avviso valido. */
    if (sys_random(raw, sizeof raw) != GS_OK) {
        goto exit;
    }
    str_addf(&tmp, "%s.tmp-%02x%02x%02x%02x", str_cstr(&file), raw[0], raw[1],
             raw[2], raw[3]);
    f = sys_fopen(str_cstr(&tmp), "wb");
    if (f == NULL) {
        goto exit;
    }
    put_rc = fprintf(f, "%s\n", self);
    close_rc = fclose(f);
    if ((put_rc >= 0) && (close_rc == 0) &&
        (sys_rename(str_cstr(&tmp), str_cstr(&file)) == 0)) {
        rc = 0;
    } else {
        (void)sys_remove(str_cstr(&tmp));
    }

exit:
    str_free(&tmp);
    str_free(&file);
    return rc;
}

int inbox_add(const char *top, const char *id, const Oid_t *oid,
              const char *from)
{
    char hex[OID_HEX_SIZE];
    Str_t dir = STR_INIT;
    int rc = -1;

    str_addf(&dir, "%s/.git", top);
    if (sys_is_dir(str_cstr(&dir)) && (make_dirs(top, id, &dir) == 0)) {
        oid_to_hex(oid, hex);
        rc = write_notice(str_cstr(&dir), hex, from);
    }
    str_free(&dir);
    return rc;
}

void inbox_notify(const char *label, const char *id, const Str_t *peers,
                  const char *self, const Oid_t *tip, PeerTally_t *t,
                  Str_t *push)
{
    Str_t dir = STR_INIT;
    const char *peer;

    for (peer = str_znext(peers, NULL); peer != NULL;
         peer = str_znext(peers, peer)) {
        str_reset(&dir);
        str_addf(&dir, "%s/.git", peer);
        if (!sys_is_dir(str_cstr(&dir)) && peers_repo_exists(peer)) {
            str_addz(push, peer); /* .git file: ripiego sul push */
        } else if (inbox_add(peer, id, tip, self) == 0) {
            t->ok++;
        } else {
            t->unreachable++;
            msg_warning(MSG_WARN_PEER_UNREACHABLE, label, peer);
        }
    }
    str_free(&dir);
}
