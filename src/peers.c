/*
 * peers.c - operazioni verso le altre repo.
 */
#include "peers.h"

#include <stdlib.h>
#include <string.h>

#include "git.h"
#include "mem.h"
#include "msg.h"
#include "sys.h"

typedef struct {
    Proc_t p;
    unsigned started : 1U;
} Job_t;

static void git_dir_arg(Str_t *arg, const char *repo)
{
    str_reset(arg);
    str_addf(arg, "--git-dir=%s/.git", repo);
}

eGsErr_t peers_of(const char *repo, const char *id, Str_t *out)
{
    const char *args[] = { NULL, "config", "--get-all", NULL, NULL };
    Str_t dir = STR_INIT;
    Str_t key = STR_INIT;
    Str_t lines = STR_INIT;
    char *line;
    char *nl;
    int code = 0;
    eGsErr_t err;

    git_dir_arg(&dir, repo);
    str_addf(&key, "shared.%s.peer", id);
    args[0] = str_cstr(&dir);
    args[3] = str_cstr(&key);
    err = git_capture(args, PROC_STDERR_NULL | PROC_CLEAN_ENV, &lines, &code);
    if ((err == GS_OK) && (code == 0)) {
        line = lines.buf;
        while ((line != NULL) && (*line != '\0')) {
            nl = strchr(line, '\n');
            if (nl != NULL) {
                *nl = '\0';
            }
            str_addz(out, line);
            line = (nl != NULL) ? (nl + 1) : NULL;
        }
    } else if ((err == GS_OK) && (code != 1)) { /* 1 = nessun peer */
        err = GS_ERR_GIT;
    } else {
        /* errore di avvio gia' in err, oppure nessun peer */
    }
    str_free(&lines);
    str_free(&key);
    str_free(&dir);
    return err;
}

void peers_incoming_ref(Str_t *ref, const char *id, unsigned k)
{
    str_reset(ref);
    str_addf(ref, "refs/shared-incoming/%s/%u", id, k);
}

void peers_push(const char *label, const char *id, const Str_t *peers,
                PeerTally_t *t)
{
    /* --no-verify: il push di refs/shared/<id> non e' un push dell'utente;
     * il suo pre-push (test, lint, lfs) non deve girare ne' bloccarlo
     * (reportIdea C-20). */
    const char *args[] = { "push", "--porcelain", "--no-verify", NULL, NULL,
                           NULL };
    size_t n = str_zcount(peers);
    Job_t *jobs = xmalloc(((n > 0U) ? n : 1U) * sizeof *jobs);
    Str_t spec = STR_INIT;
    Str_t out = STR_INIT;
    const char *peer;
    const char *o;
    size_t k;
    int code;
    int rejected;

    str_addf(&spec, "refs/shared/%s:refs/shared/%s", id, id);
    args[4] = str_cstr(&spec);
    for (k = 0U, peer = str_znext(peers, NULL); peer != NULL;
         k++, peer = str_znext(peers, peer)) {
        args[3] = peer;
        jobs[k].started = (git_start(&jobs[k].p, args, PROC_STDERR_NULL) == GS_OK);
    }
    for (k = 0U, peer = str_znext(peers, NULL); peer != NULL;
         k++, peer = str_znext(peers, peer)) {
        code = -1;
        if (jobs[k].started && (proc_collect(&jobs[k].p, &out, &code) != GS_OK)) {
            code = -1;
        }
        /* --porcelain: una riga per ref, "!" in testa se rifiutata. */
        o = str_cstr(&out);
        rejected = (code == 1) &&
                   ((strncmp(o, "!\t", 2U) == 0) || (strstr(o, "\n!\t") != NULL));
        if (code == 0) {
            t->ok++;
        } else if (rejected) {
            t->rejected++;
            msg_error(MSG_ERR_PEER_REJECTED, label, peer);
        } else {
            t->unreachable++;
            msg_warning(MSG_WARN_PEER_UNREACHABLE, label, peer);
        }
        str_reset(&out);
    }
    str_free(&out);
    str_free(&spec);
    free(jobs);
}

void peers_fetch(const char *label, const char *id, const Str_t *peers,
                 uint8_t *ok, PeerTally_t *t)
{
    /* Fetch paralleli nella stessa repo: niente FETCH_HEAD condiviso e
     * niente gc automatico a meta' (git >= 2.29). */
    const char *args[] = { "fetch", "-q", "--no-tags", "--no-write-fetch-head",
                           "--no-auto-gc", NULL, NULL, NULL };
    size_t n = str_zcount(peers);
    Job_t *jobs = xmalloc(((n > 0U) ? n : 1U) * sizeof *jobs);
    Str_t spec = STR_INIT;
    Str_t ref = STR_INIT;
    Str_t out = STR_INIT;
    const char *peer;
    size_t k;
    int code;

    for (k = 0U, peer = str_znext(peers, NULL); peer != NULL;
         k++, peer = str_znext(peers, peer)) {
        peers_incoming_ref(&ref, id, (unsigned)k);
        str_reset(&spec);
        str_addf(&spec, "+refs/shared/%s:%s", id, str_cstr(&ref));
        args[5] = peer;
        args[6] = str_cstr(&spec);
        jobs[k].started = (git_start(&jobs[k].p, args, PROC_STDERR_NULL) == GS_OK);
    }
    for (k = 0U, peer = str_znext(peers, NULL); peer != NULL;
         k++, peer = str_znext(peers, peer)) {
        code = -1;
        if (jobs[k].started && (proc_collect(&jobs[k].p, &out, &code) != GS_OK)) {
            code = -1;
        }
        ok[k] = (code == 0) ? 1U : 0U;
        if (code == 0) {
            t->ok++;
        } else {
            t->unreachable++;
            msg_warning(MSG_WARN_PEER_UNREACHABLE, label, peer);
        }
    }
    str_free(&out);
    str_free(&ref);
    str_free(&spec);
    free(jobs);
}

int peers_repo_exists(const char *repo)
{
    Str_t path = STR_INIT;
    FILE *f;
    int found;

    str_addf(&path, "%s/.git", repo);
    found = sys_is_dir(str_cstr(&path));
    if (!found) {
        f = sys_fopen(str_cstr(&path), "rb");
        found = (f != NULL);
        if (f != NULL) {
            (void)fclose(f);
        }
    }
    str_free(&path);
    return found;
}

void peers_check(const char *id, const Str_t *peers, uint8_t *state)
{
    /* config --local --get-regexp sulla sezione: 0 = c'e', 1 = non c'e'.
     * Senza --local, con --git-dir verso una cartella che non esiste git
     * legge solo la configurazione globale e risponde 1 (provato con git
     * 2.43): un peer cancellato sembrerebbe "uscito". */
    const char *args[] = { NULL, "config", "--local", "--get-regexp", NULL,
                           NULL };
    size_t n = str_zcount(peers);
    Job_t *jobs = xmalloc(((n > 0U) ? n : 1U) * sizeof *jobs);
    Str_t regex = STR_INIT;
    Str_t dir = STR_INIT;
    Str_t out = STR_INIT;
    const char *peer;
    size_t k;
    int code;

    str_addf(&regex, "^shared\\.%s\\.", id);
    args[4] = str_cstr(&regex);
    for (k = 0U, peer = str_znext(peers, NULL); peer != NULL;
         k++, peer = str_znext(peers, peer)) {
        git_dir_arg(&dir, peer);
        args[0] = str_cstr(&dir);
        jobs[k].started = (git_start(&jobs[k].p, args,
                                     PROC_STDERR_NULL | PROC_CLEAN_ENV) == GS_OK);
    }
    for (k = 0U, peer = str_znext(peers, NULL); peer != NULL;
         k++, peer = str_znext(peers, peer)) {
        code = -1;
        if (jobs[k].started && (proc_collect(&jobs[k].p, &out, &code) != GS_OK)) {
            code = -1;
        }
        if (code == 0) {
            state[k] = (uint8_t)PEER_MEMBER;
        } else if ((code == 1) && peers_repo_exists(peer)) {
            state[k] = (uint8_t)PEER_LEFT;
        } else if (!peers_repo_exists(peer)) {
            state[k] = (uint8_t)PEER_GONE;
        } else {
            state[k] = (uint8_t)PEER_UNREACHABLE;
        }
    }
    str_free(&out);
    str_free(&dir);
    str_free(&regex);
    free(jobs);
}

void peers_value_regex(Str_t *out, const char *a, const char *b)
{
    const char *const vals[] = { a, b };
    const char *c;
    size_t i;

    str_reset(out);
    str_add(out, "^(");
    for (i = 0U; i < 2U; i++) {
        if (i > 0U) {
            str_addch(out, '|');
        }
        for (c = vals[i]; *c != '\0'; c++) {
            if (strchr(".[]()*+?{}|^$\\", *c) != NULL) {
                str_addch(out, '\\');
            }
            str_addch(out, *c);
        }
    }
    str_add(out, ")$");
}

void peers_replace(const char *label, const char *id, const Str_t *peers,
                   const char *old, const char *new_path, PeerTally_t *t)
{
    /* --replace-all con un'espressione su old|new: un solo processo per
     * peer, nessun doppione anche se relocate si ripete. */
    const char *args[] = { NULL, "config", "--replace-all", NULL, new_path,
                           NULL, NULL };
    size_t n = str_zcount(peers);
    Job_t *jobs = xmalloc(((n > 0U) ? n : 1U) * sizeof *jobs);
    Str_t key = STR_INIT;
    Str_t regex = STR_INIT;
    Str_t dir = STR_INIT;
    Str_t out = STR_INIT;
    const char *peer;
    size_t k;
    int code;

    str_addf(&key, "shared.%s.peer", id);
    peers_value_regex(&regex, old, new_path);
    args[3] = str_cstr(&key);
    args[5] = str_cstr(&regex);
    for (k = 0U, peer = str_znext(peers, NULL); peer != NULL;
         k++, peer = str_znext(peers, peer)) {
        git_dir_arg(&dir, peer);
        args[0] = str_cstr(&dir);
        jobs[k].started = (git_start(&jobs[k].p, args,
                                     PROC_STDERR_NULL | PROC_CLEAN_ENV) == GS_OK);
    }
    for (k = 0U, peer = str_znext(peers, NULL); peer != NULL;
         k++, peer = str_znext(peers, peer)) {
        code = -1;
        if (jobs[k].started && (proc_collect(&jobs[k].p, &out, &code) != GS_OK)) {
            code = -1;
        }
        if (code == 0) {
            t->ok++;
        } else {
            t->unreachable++;
            msg_warning(MSG_WARN_PEER_UNREACHABLE, label, peer);
        }
    }
    str_free(&out);
    str_free(&dir);
    str_free(&regex);
    str_free(&key);
    free(jobs);
}

void peers_forget(const char *label, const char *id, const Str_t *peers,
                  const char *self, PeerTally_t *t)
{
    /* --fixed-value --unset-all: toglie solo la riga uguale a self. */
    const char *args[] = { NULL, "config", "--fixed-value", "--unset-all",
                           NULL, self, NULL };
    size_t n = str_zcount(peers);
    Job_t *jobs = xmalloc(((n > 0U) ? n : 1U) * sizeof *jobs);
    Str_t key = STR_INIT;
    Str_t dir = STR_INIT;
    Str_t out = STR_INIT;
    const char *peer;
    size_t k;
    int code;

    str_addf(&key, "shared.%s.peer", id);
    args[4] = str_cstr(&key);
    for (k = 0U, peer = str_znext(peers, NULL); peer != NULL;
         k++, peer = str_znext(peers, peer)) {
        git_dir_arg(&dir, peer);
        args[0] = str_cstr(&dir);
        jobs[k].started = (git_start(&jobs[k].p, args,
                                     PROC_STDERR_NULL | PROC_CLEAN_ENV) == GS_OK);
    }
    for (k = 0U, peer = str_znext(peers, NULL); peer != NULL;
         k++, peer = str_znext(peers, peer)) {
        code = -1;
        if (jobs[k].started && (proc_collect(&jobs[k].p, &out, &code) != GS_OK)) {
            code = -1;
        }
        if ((code == 0) || (code == 5)) { /* 5 = non c'era */
            t->ok++;
        } else {
            t->unreachable++;
            msg_warning(MSG_WARN_PEER_UNREACHABLE, label, peer);
        }
    }
    str_free(&out);
    str_free(&dir);
    str_free(&key);
    free(jobs);
}

void peers_announce(const char *id, const Str_t *peers, const char *self,
                    PeerTally_t *t)
{
    /* --replace-all --fixed-value: sostituisce la riga uguale a self o la
     * aggiunge se manca. Un processo per peer, nessun duplicato. */
    const char *args[] = { NULL, "config", "--replace-all", "--fixed-value",
                           NULL, self, self, NULL };
    size_t n = str_zcount(peers);
    Job_t *jobs = xmalloc(((n > 0U) ? n : 1U) * sizeof *jobs);
    Str_t key = STR_INIT;
    Str_t dir = STR_INIT;
    Str_t out = STR_INIT;
    const char *peer;
    size_t k;
    int code;

    str_addf(&key, "shared.%s.peer", id);
    args[4] = str_cstr(&key);
    for (k = 0U, peer = str_znext(peers, NULL); peer != NULL;
         k++, peer = str_znext(peers, peer)) {
        git_dir_arg(&dir, peer);
        args[0] = str_cstr(&dir);
        jobs[k].started = (git_start(&jobs[k].p, args,
                                     PROC_STDERR_NULL | PROC_CLEAN_ENV) == GS_OK);
    }
    for (k = 0U, peer = str_znext(peers, NULL); peer != NULL;
         k++, peer = str_znext(peers, peer)) {
        code = -1;
        if (jobs[k].started && (proc_collect(&jobs[k].p, &out, &code) != GS_OK)) {
            code = -1;
        }
        if (code == 0) {
            t->ok++;
        } else {
            t->unreachable++;
            msg_warning(MSG_WARN_PEER_UNREACHABLE, id, peer);
        }
    }
    str_free(&out);
    str_free(&dir);
    str_free(&key);
    free(jobs);
}
