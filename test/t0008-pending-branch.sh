#!/bin/sh
# t0008-pending-branch.sh - attese di set/link e cambio di branch
# (REQ-F-023, D-059, D-077; reportIdea B-29).

. "$(dirname "$0")/lib.sh"

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
expect_eq "attesa: branch registrato" "$(pending_field "$ID" branch)" "main"
expect_eq "attesa: path registrato" "$(pending_field "$ID" path)" "c.h"

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
expect_eq "B-29: attesa tolta" "$(pending_op "$ID")" ""
expect_eq "B-29: nessuna chiave dell'attesa" \
	"$(git config --name-only --get-regexp "^shared[.]$ID[.]pending" | wc -l | tr -d ' ')" "0"
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
expect_eq "restore: sezione locale tolta" "$(pending_op "$ID")" ""

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
expect_eq "branch cancellato: l'attesa resta" "$(pending_op "$ID")" "set"

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

# --- B-39: commit di conferma su un altro branch, primo comando sul branch ------
# --- del comando: non si scarta, si completa da li' (D-079) ---------------------
make_repo n
cd "$TRASH/n" || exit 1
put o.txt o
put c.h v1
git shared set c.h >/dev/null
ID=$(shared_id c.h)
git switch -q -c feat
git commit -q -m "share c.h"
git switch -q main
run git shared status
case "$ERR" in
*discarded*) not_ok "B-39: nessuno scarto" "$ERR" ;;
*) ok "B-39: nessuno scarto" ;;
esac
expect_ok "B-39: completato dal branch che lo dichiara" has_chain "$ID"
expect_eq "B-39: attesa tolta" "$(pending_op "$ID")" ""
expect_out "B-39: su main la sezione non c'e' (B-16)" "suspended on this branch (not declared here)"
git switch -q feat
run git shared status
expect_out "B-39: su feat collegato (attivo solo main)" \
	"suspended on this branch (aligned)"

# Molti branch con lo stesso .gitshared, uno solo con la sezione.
make_repo q
cd "$TRASH/q" || exit 1
put o.txt o
for i in 1 2 3 4 5; do git branch "b$i"; done
put c.h v1
git shared set c.h >/dev/null
ID=$(shared_id c.h)
git switch -q -c z-last
git commit -q -m "share c.h"
git switch -q main
run git shared status
expect_ok "B-39 con piu' branch: completato" has_chain "$ID"

# --- B-40: attesa scartata durante un git stash, poi set riprende l'ID (D-079) ---
make_repo p
cd "$TRASH/p" || exit 1
put o.txt o
put c.h v1
git shared set c.h >/dev/null
ID=$(shared_id c.h)
git stash -q
run git shared status
expect_err "B-40: scartata con lo stash" "discarded pending operation on 'c.h'"
git stash pop -q
run git shared status
expect_out "B-40: dopo stash pop scollegato" "unlinked"
run git shared set c.h
expect_rc "B-40: set riprende: codice 0" 0
expect_out "B-40: set riprende: da committare" "'c.h' is now shared as 'c.h'. Commit .gitshared"
expect_eq "B-40: stesso ID" "$(shared_id c.h)" "$ID"
expect_eq "B-40: una sola sezione" \
	"$(git config -f .gitshared --get-regexp '[.]id$' | wc -l | tr -d ' ')" "1"
expect_eq "B-40: di nuovo in attesa" "$(pending_op "$ID")" "set"
git commit -q -m "share c.h"
run git shared status
expect_out "B-40: allineato dopo il commit" "aligned"
run git shared set c.h
expect_rc "set su un file collegato: ancora rifiutato" 1
expect_err "set su un file collegato: errore" "already shared"

# Sezione gia' committata quando si riprende: vale subito, senza commit.
make_repo t
cd "$TRASH/t" || exit 1
put o.txt o
put c.h v1
git shared set c.h >/dev/null
ID=$(shared_id c.h)
git stash -q
git shared status >/dev/null 2>&1
git stash pop -q
git add .gitshared
git commit -q -m "share c.h"
run git shared set c.h
expect_rc "ripresa gia' committata: codice 0" 0
expect_no_out "ripresa gia' committata: nessun commit richiesto" "Commit .gitshared"
expect_out "ripresa gia' committata: confermato" "'c.h' is now shared as 'c.h'."
expect_ok "ripresa gia' committata: catena creata" has_chain "$ID"
run git shared status
expect_out "ripresa gia' committata: allineato" "aligned"

# --- link scartato da uno stash: link lo riprende (D-079) ------------------------
make_repo LA
cd "$TRASH/LA" || exit 1
put l.h v1
git shared set -c l.h >/dev/null
LID=$(shared_id l.h)
make_repo LB
cd "$TRASH/LB" || exit 1
put o.txt o
git shared link ../LA/l.h >/dev/null
git stash -q -u
run git shared status
expect_err "link e stash: scartato" "discarded pending operation on 'l.h'"
git stash pop -q
run git shared status
expect_out "link e stash: dopo stash pop scollegato" "unlinked"
run git shared link ../LA/l.h other.h
expect_rc "link e stash: destinazione diversa rifiutata" 1
expect_err "link e stash: messaggio" "declared here at 'l.h'"
run git shared link ../LA/l.h
expect_rc "link e stash: link riprende: codice 0" 0
expect_out "link e stash: da committare" "Commit it with .gitshared to confirm"
expect_eq "link e stash: stesso ID" "$(shared_id l.h)" "$LID"
expect_eq "link e stash: una sola sezione" \
	"$(git config -f .gitshared --get-regexp '[.]id$' | wc -l | tr -d ' ')" "1"
expect_eq "link e stash: file e .gitshared in staging" \
	"$(git diff --cached --name-only | sort | tr '\n' ' ')" ".gitshared l.h "
git commit -q -m "link l.h"
run git shared status
expect_out "link e stash: allineato dopo il commit" "aligned"
expect_eq "link e stash: LA conosce LB" \
	"$(git -C ../LA config --get-all "shared.$LID.peer")" "$(git rev-parse --show-toplevel)"
run git shared link ../LA/l.h
expect_rc "link su un file collegato: ancora rifiutato" 1
expect_err "link su un file collegato: errore" "already shared here"

# --- clone: link ricollega la copia ai peer (D-043) ------------------------------
git clone -q "$TRASH/LA" "$TRASH/LC"
cd "$TRASH/LC" || exit 1
run git shared status
expect_out "clone: scollegato" "unlinked"
printf 'modifica locale non committata\n' >>l.h
run git shared link ../LA/l.h
expect_rc "clone: link: codice 0" 0
expect_out "clone: collegato subito" "'l.h' linked as 'l.h' from"
expect_no_out "clone: nessun commit richiesto" "Commit it"
expect_ok "clone: file non toccato" grep -q "modifica locale non committata" l.h
expect_eq "clone: modifica dell'utente non in staging" "$(git diff --cached --name-only)" ""
expect_ok "clone: catena ricevuta" git rev-parse -q --verify "refs/shared/$LID"
expect_eq "clone: catena uguale alla sorgente" \
	"$(git rev-parse "refs/shared/$LID")" "$(git -C ../LA rev-parse "refs/shared/$LID")"
expect_ok "clone: hook installati" grep -q "git shared" .git/hooks/post-merge
expect_eq "clone: branch attivi dal default" "$(git config --get-all "shared.$LID.branch")" "main"
case "$(git -C ../LA config --get-all "shared.$LID.peer")" in
*"$(git rev-parse --show-toplevel)"*) ok "clone: LA conosce il clone" ;;
*) not_ok "clone: LA conosce il clone" ;;
esac
case "$(git config --get-all "shared.$LID.peer")" in
*LB*) ok "clone: il clone conosce anche LB" ;;
*) not_ok "clone: il clone conosce anche LB" "$(git config --get-all "shared.$LID.peer")" ;;
esac
git checkout -q -- l.h
run git shared status
expect_out "clone: allineato" "aligned"
cd "$TRASH/LA" || exit 1
put l.h v2
run git shared publish
expect_out "clone: publish arriva a LB e al clone" "2 of 2 peers notified"
cd "$TRASH/LC" || exit 1
run git shared update -c
expect_out "clone: update dal peer" "new version applied and committed"

# --- D-113: attesa in una chiave; il formato fino alla v0.9.0 si legge ancora ---
# pending_count <id>: chiavi dell'attesa (pending, pendingXxx) nella sezione
pending_count() {
	git config --name-only --get-regexp "^shared[.]$1[.]pending" | wc -l | tr -d ' '
}

make_repo v
cd "$TRASH/v" || exit 1
put o.txt o
git branch old
put c.h v1
git shared set c.h >/dev/null
ID=$(shared_id c.h)
expect_eq "D-113: una chiave sola" "$(pending_count "$ID")" "1"
expect_eq "D-113: campi vuoti per set" \
	"$(pending_field "$ID" remote)$(pending_field "$ID" commit)$(pending_field "$ID" source)" ""

# set della v0.9.0: branch e path letti dalle chiavi vecchie
git config "shared.$ID.pending" set
git config "shared.$ID.pendingPath" c.h
git config "shared.$ID.pendingBranch" main
git restore --staged .gitshared
mv .gitshared ../v.gitshared
git checkout -q old
run git shared status
case "$ERR" in
*discarded*) not_ok "v0.9.0: da old non si scarta" "$ERR" ;;
*) ok "v0.9.0: da old non si scarta" ;;
esac
expect_out "v0.9.0: branch dalla chiave vecchia" "pending commit (on branch main)"
expect_out "v0.9.0: path dalla chiave vecchia" "c.h"
git checkout -q main
mv ../v.gitshared .gitshared
git add .gitshared
git commit -q -m "share c.h" 2>/dev/null
expect_ok "v0.9.0: completato al commit" has_chain "$ID"
expect_eq "v0.9.0: tutte le chiavi tolte" "$(pending_count "$ID")" "0"

# chiavi vecchie rimaste da un'attesa interrotta: la chiave nuova vale
put d.h v1
git shared set d.h >/dev/null
DID=$(shared_id d.h)
git config "shared.$DID.pendingBranch" ghost
git config "shared.$DID.pendingPath" ghost.h
run git shared status
expect_no_out "chiavi rimaste: ignorate" "ghost"
git commit -q -m "share d.h" 2>/dev/null
expect_ok "chiavi rimaste: completato" has_chain "$DID"
expect_eq "chiavi rimaste: tolte anche loro" "$(pending_count "$DID")" "0"

# link della v0.9.0: la sorgente dalla chiave vecchia, per la membership
make_repo vs
cd "$TRASH/vs" || exit 1
put w.h v1
git shared set -c w.h >/dev/null
WID=$(shared_id w.h)
make_repo vd
cd "$TRASH/vd" || exit 1
put o.txt o
git shared link ../vs/w.h >/dev/null
SRC=$(pending_field "$WID" source)
expect_eq "D-113: sorgente nella chiave" "$SRC" "$(git -C ../vs rev-parse --show-toplevel)"
expect_eq "D-113: link locale" "$(pending_field "$WID" remote)" "false"
git config "shared.$WID.pending" link
git config "shared.$WID.pendingSource" "$SRC"
git config "shared.$WID.pendingPath" w.h
git config "shared.$WID.pendingBranch" main
git config "shared.$WID.pendingRemote" false
git commit -q -m "link w.h" 2>/dev/null
expect_ok "v0.9.0 link: completato" has_chain "$WID"
expect_eq "v0.9.0 link: la sorgente conosce la destinazione" \
	"$(git -C ../vs config --get-all "shared.$WID.peer")" "$(git rev-parse --show-toplevel)"
expect_eq "v0.9.0 link: tutte le chiavi tolte" "$(pending_count "$WID")" "0"

done_testing
