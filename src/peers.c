/*
 * peers.c - operazioni verso le altre repo.
 */
#include "peers.h"

#include <stdlib.h>
#include <string.h>

#include "git.h"
#include "mem.h"
#include "msg.h"

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
    err = git_capture(args, PROC_STDERR_NULL, &lines, &code);
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
    const char *args[] = { "push", "--porcelain", NULL, NULL, NULL };
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
    args[3] = str_cstr(&spec);
    for (k = 0U, peer = str_znext(peers, NULL); peer != NULL;
         k++, peer = str_znext(peers, peer)) {
        args[2] = peer;
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
        jobs[k].started = (git_start(&jobs[k].p, args, PROC_STDERR_NULL) == GS_OK);
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
