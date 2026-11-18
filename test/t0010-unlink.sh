#!/bin/sh
# t0010-unlink.sh - git shared unlink (REQ-F-003, REQ-F-023, D-081;
# reportIdea B-45).

. "$(dirname "$0")/lib.sh"

pending_key() {
	git config "shared.$1.$2" 2>/dev/null
}

has_chain() {
	git rev-parse -q --verify "refs/shared/$1" >/dev/null
}

has_section() {
	git config --get-regexp "^shared[.]$1[.]" >/dev/null 2>&1
}

top() {
	git -C "$TRASH/$1" rev-parse --show-toplevel
}

# --- argomenti ------------------------------------------------------------------
make_repo r
cd "$TRASH/r" || exit 1
put o.txt o
put c.h v1
git shared set -c c.h >/dev/null
ID=$(shared_id c.h)
run git shared unlink
expect_rc "senza argomenti: codice 129" 129
run git shared unlink a b
expect_rc "due argomenti: codice 129" 129
run git shared unlink o.txt
expect_rc "file non shared: codice 1" 1
expect_err "file non shared: errore" "'o.txt' is not a shared file"
run git shared unlink -h
expect_out "uso: breve" "Stop sharing <path>. The file stays, as a regular file."
run git shared unlink --help
expect_out "manuale: unlink" "Stops sharing a file. The file stays, as a regular file."

# --- al comando: solo la repo corrente, in attesa -------------------------------
run git shared unlink c.h
expect_rc "unlink: codice 0" 0
expect_out "unlink: messaggio" "'c.h' is no longer shared. Commit .gitshared to confirm."
expect_fail "unlink: .gitshared vuoto, tolto" test -f .gitshared
expect_eq "unlink: cancellazione in staging" "$(git diff --cached --name-status)" "D	.gitshared"
expect_ok "unlink: il file resta" test -f c.h
expect_eq "unlink: attesa" "$(pending_key "$ID" pending)" "unlink"
expect_eq "unlink: commit del comando" "$(pending_key "$ID" pendingCommit)" "$(git rev-parse HEAD)"
expect_ok "unlink: catena ancora presente" has_chain "$ID"
run git shared status
expect_out "unlink: in attesa di commit" "pending commit"
expect_out "unlink: path mostrato" "c.h"
run git shared unlink c.h
expect_rc "unlink ripetuto: codice 1" 1

# --- annullamento con git restore -------------------------------------------------
git restore --staged --worktree .gitshared
run git shared status
expect_err "restore: unlink annullato" "discarded pending operation on 'c.h'"
expect_out "restore: di nuovo allineato" "aligned"
expect_eq "restore: attesa tolta" "$(pending_key "$ID" pending)$(pending_key "$ID" pendingCommit)" ""
expect_ok "restore: catena intatta" has_chain "$ID"

# --- commit: dati locali tolti ---------------------------------------------------
git shared unlink c.h >/dev/null
git commit -q -m "unshare c.h"
run git shared status
expect_out "commit: nessun file shared" "No shared files in this repository."
expect_fail "commit: catena tolta" has_chain "$ID"
expect_fail "commit: sezione locale tolta" has_section "$ID"
expect_eq "commit: il file resta tracciato" "$(git ls-files c.h)" "c.h"

# --- -c con due file: .gitshared resta con l'altro -------------------------------
make_repo two
cd "$TRASH/two" || exit 1
put a.h a
put b.h b
git shared set -c a.h >/dev/null
git shared set -c b.h >/dev/null
AID=$(shared_id a.h)
run git shared unlink -c a.h
expect_rc "unlink -c: codice 0" 0
expect_out "unlink -c: confermato" "'a.h' is no longer shared: it is now a regular file."
expect_eq "unlink -c: messaggio standard" "$(git log -1 --format=%s)" "Unshare a.h"
expect_eq "unlink -c: solo .gitshared nel commit" "$(git show --name-only --format= HEAD)" ".gitshared"
expect_eq "unlink -c: resta b.h" "$(git config -f .gitshared --get-regexp '[.]path$')" "shared.b.h.path b.h"
expect_fail "unlink -c: catena di a.h tolta" has_chain "$AID"
run git shared status
expect_out "unlink -c: b.h allineato" "aligned"
expect_no_out "unlink -c: a.h sparito" "a.h"
git shared unlink -m "via anche b" b.h >/dev/null
expect_eq "unlink -m: messaggio" "$(git log -1 --format=%s)" "via anche b"
expect_eq "unlink -m: .gitshared tolto dal commit" "$(git ls-files .gitshared)" ""

# --- peer: dimenticano la repo ---------------------------------------------------
make_repo A
cd "$TRASH/A" || exit 1
put p.h v1
git shared set -c p.h >/dev/null
PID=$(shared_id p.h)
for R in B C; do
	make_repo $R
	cd "$TRASH/$R" || exit 1
	put o.txt o
	git shared link -c ../A/p.h >/dev/null
done
cd "$TRASH/B" || exit 1
run git shared unlink -c p.h
expect_rc "peer: unlink -c: codice 0" 0
expect_eq "peer: A non conosce piu' B" "$(git -C ../A config --get-all "shared.$PID.peer")" "$(top C)"
expect_eq "peer: C non conosce piu' B" "$(git -C ../C config --get-all "shared.$PID.peer")" "$(top A)"
cd "$TRASH/A" || exit 1
put p.h v2
run git shared publish
expect_out "peer: A pubblica solo a C" "1 of 1 peers notified"
expect_fail "peer: B non riceve piu'" git -C ../B rev-parse -q --verify "refs/shared/$PID"

# Peer irraggiungibile: avviso, unlink completato lo stesso.
cd "$TRASH/C" || exit 1
mv "$TRASH/A" "$TRASH/A.off"
run git shared unlink -c p.h
expect_rc "peer irraggiungibile: codice 0" 0
expect_err "peer irraggiungibile: avviso" "not reachable"
expect_fail "peer irraggiungibile: catena tolta" has_chain "$PID"
mv "$TRASH/A.off" "$TRASH/A"

# --- commit su un altro branch (come B-39) ---------------------------------------
make_repo m
cd "$TRASH/m" || exit 1
put o.txt o
put c.h v1
git shared set -c c.h >/dev/null
ID=$(shared_id c.h)
git branch old
git shared unlink c.h >/dev/null
git switch -q -c feat
git commit -q -m "unshare c.h"
git switch -q main
run git shared status
case "$ERR" in
*discarded*) not_ok "altro branch: nessun annullamento" "$ERR" ;;
*) ok "altro branch: nessun annullamento" ;;
esac
expect_fail "altro branch: completato" has_chain "$ID"
expect_out "altro branch: su main resta dichiarato" "unlinked"

# Un branch nato prima del comando non conferma l'unlink.
make_repo k
cd "$TRASH/k" || exit 1
put o.txt o
git branch before
put c.h v1
git shared set -c c.h >/dev/null
ID=$(shared_id c.h)
git shared unlink c.h >/dev/null
git restore --staged --worktree .gitshared
git branch -f feat before
run git shared status
expect_err "branch vecchio: annullato, non completato" "discarded pending operation"
expect_ok "branch vecchio: catena intatta" has_chain "$ID"

# --- stato non adatto ------------------------------------------------------------
make_repo p
cd "$TRASH/p" || exit 1
put o.txt o
put c.h v1
git shared set c.h >/dev/null
run git shared unlink c.h
expect_rc "set in attesa: rifiutato" 1
expect_err "set in attesa: messaggio" "has a pending operation"

# --- file scollegato (clone): si toglie solo la sezione ---------------------------
git clone -q "$TRASH/A" "$TRASH/clone"
cd "$TRASH/clone" || exit 1
BEFORE=$(git -C ../A config --get-all "shared.$PID.peer")
run git shared unlink -c p.h
expect_rc "scollegato: codice 0" 0
expect_out "scollegato: confermato" "no longer shared"
expect_eq "scollegato: A non toccata" "$(git -C ../A config --get-all "shared.$PID.peer")" "$BEFORE"
run git shared status
expect_out "scollegato: nessun file shared" "No shared files"

# --- file cancellato (incoerente) -------------------------------------------------
make_repo del
cd "$TRASH/del" || exit 1
put c.h v1
put o.txt o
git shared set -c c.h >/dev/null
ID=$(shared_id c.h)
git rm -q c.h
git commit -q -m "rm c.h"
run git shared status
expect_out "cancellato: incoerente" "inconsistent"
run git shared unlink -c c.h
expect_rc "cancellato: unlink riesce" 0
expect_fail "cancellato: catena tolta" has_chain "$ID"

# --- sezione locale orfana, per ID (reportIdea B-45) ------------------------------
make_repo orf
cd "$TRASH/orf" || exit 1
put o.txt o
put c.h v1
git shared set -c c.h >/dev/null
ID=$(shared_id c.h)
git reset -q --hard HEAD~1
run git shared status
expect_out "orfana: incoerente" "inconsistent"
run git shared unlink "$ID"
expect_rc "orfana: codice 0" 0
expect_out "orfana: messaggio" "Removed the leftover local data of $ID."
expect_fail "orfana: catena tolta" has_chain "$ID"
run git shared status
expect_out "orfana: nessun file shared" "No shared files"

# Branch nato prima del set (B-16): non e' orfana, si rifiuta.
make_repo b16
cd "$TRASH/b16" || exit 1
put o.txt o
git branch old
put c.h v1
git shared set -c c.h >/dev/null
ID=$(shared_id c.h)
git switch -q old
run git shared unlink "$ID"
expect_rc "branch vecchio: per ID rifiutato" 1
expect_err "branch vecchio: dove farlo" "still shared on branch 'main'"
expect_ok "branch vecchio: catena intatta" has_chain "$ID"

done_testing
