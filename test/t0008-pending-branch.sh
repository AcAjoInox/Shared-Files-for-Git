#!/bin/sh
# t0008-pending-branch.sh - attese di set/link e cambio di branch
# (REQ-F-023, D-059, D-077; reportIdea B-29).

. "$(dirname "$0")/lib.sh"

# pending_key <id> <campo>: valore della chiave dell'attesa ("" se assente)
pending_key() {
	git config "shared.$1.$2" 2>/dev/null
}

# has_chain <id>: la catena esiste
has_chain() {
	git rev-parse -q --verify "refs/shared/$1" >/dev/null
}

# --- registrazione dell'attesa -----------------------------------------------
make_repo r
cd "$TRASH/r" || exit 1
put o.txt o
git branch old
put c.h v1
git shared set c.h >/dev/null
ID=$(shared_id c.h)
expect_eq "attesa: branch registrato" "$(pending_key "$ID" pendingBranch)" "main"
expect_eq "attesa: path registrato" "$(pending_key "$ID" pendingPath)" "c.h"

# --- B-29: set committato, primo comando da un altro branch -------------------
git commit -q -m "share c.h"
git checkout -q old
run git shared status
expect_no_out "B-29: nessuno scarto" "discarded"
case "$ERR" in
*discarded*) not_ok "B-29: nessun avviso di scarto" "$ERR" ;;
*) ok "B-29: nessun avviso di scarto" ;;
esac
expect_ok "B-29: completato da old (catena creata)" has_chain "$ID"
expect_eq "B-29: attesa tolta" "$(pending_key "$ID" pending)" ""
expect_eq "B-29: chiavi accessorie tolte" \
	"$(pending_key "$ID" pendingBranch)$(pending_key "$ID" pendingPath)" ""
expect_eq "B-29: branch attivi dal default (main)" \
	"$(git config --get-all "shared.$ID.branch")" "main"
git checkout -q main
run git shared status
expect_out "B-29: su main allineato" "aligned"

# --- set non committato, .gitshared assente sull'altro branch -----------------
make_repo s
cd "$TRASH/s" || exit 1
put o.txt o
git branch old
put c.h v1
git shared set c.h >/dev/null
ID=$(shared_id c.h)
expect_ok "non committato: git porta .gitshared su old" git checkout -q old
run git shared status
expect_out "non committato su old: in attesa" "pending commit"
expect_eq "non committato su old: nessun avviso" "$ERR" ""

# ...e commit fatto su old (put committa anche .gitshared in staging): si
# completa lo stesso, da HEAD.
put c.h v1
run git shared status
expect_ok "commit su un altro branch: completato" has_chain "$ID"
expect_out "commit su un altro branch: sospeso (attivo solo main)" \
	"suspended on this branch (aligned)"

# --- set non committato, .gitshared diverso sull'altro branch -----------------
make_repo d
cd "$TRASH/d" || exit 1
put o.txt o
git checkout -q -b old
put d.h d
git shared set -c d.h >/dev/null
git checkout -q main
put c.h v1
git shared set c.h >/dev/null
ID=$(shared_id c.h)
run git checkout old
expect_rc "conflitto su .gitshared: git rifiuta lo switch" 1
run git shared status
expect_out "conflitto su .gitshared: ancora in attesa" "pending commit"

# --- annullamento con git restore sul branch del comando ----------------------
git restore --staged --worktree .gitshared
run git shared status
expect_err "restore: attesa scartata, con il path" \
	"discarded pending operation on 'c.h'"
expect_eq "restore: sezione locale tolta" "$(pending_key "$ID" pending)" ""

# --- restore, poi comando da un altro branch: si decide solo sul branch -------
make_repo e
cd "$TRASH/e" || exit 1
put o.txt o
git branch old
put c.h v1
git shared set c.h >/dev/null
ID=$(shared_id c.h)
git restore --staged .gitshared
rm .gitshared
git checkout -q old
run git shared status
case "$ERR" in
*discarded*) not_ok "altro branch: non si scarta" "$ERR" ;;
*) ok "altro branch: non si scarta" ;;
esac
expect_out "altro branch: in attesa su main" "pending commit (on branch main)"
expect_out "altro branch: path mostrato" "c.h"
git checkout -q main
run git shared status
expect_err "tornati su main: scartata" "discarded pending operation on 'c.h'"

# --- branch del comando cancellato prima del commit ---------------------------
make_repo g
cd "$TRASH/g" || exit 1
put o.txt o
git checkout -q -b tmp
put c.h v1
git shared set c.h >/dev/null
ID=$(shared_id c.h)
git stash -q
git checkout -q main
git branch -q -D tmp
run git shared status
expect_err "branch cancellato: avviso" "on branch 'tmp', which no longer exists"
expect_err "branch cancellato: come toglierla" "git config --remove-section shared.$ID"
expect_eq "branch cancellato: l'attesa resta" "$(pending_key "$ID" pending)" "set"

# --- B-29 per link -------------------------------------------------------------
make_repo A
cd "$TRASH/A" || exit 1
put l.h v1
git shared set -c l.h >/dev/null
LID=$(shared_id l.h)
make_repo B
cd "$TRASH/B" || exit 1
put o.txt o
git branch old
git shared link ../A/l.h >/dev/null
git commit -q -m "link l.h"
git checkout -q old
run git shared status
case "$ERR" in
*discarded*) not_ok "link da old: nessuno scarto" "$ERR" ;;
*) ok "link da old: nessuno scarto" ;;
esac
expect_ok "link da old: catena creata" has_chain "$LID"
expect_eq "link da old: A conosce B" \
	"$(git -C ../A config --get-all "shared.$LID.peer")" \
	"$(git rev-parse --show-toplevel)"
expect_fail "link da old: ref temporanea tolta" \
	git rev-parse -q --verify "refs/shared-incoming/$LID/0"
git checkout -q main
run git shared status
expect_out "link: su main allineato" "aligned"

done_testing
