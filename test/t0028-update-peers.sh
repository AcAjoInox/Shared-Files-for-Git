#!/bin/sh
# t0028-update-peers.sh - update di un'altra repo e di tutti i peer (D-115).
# REQ-F-009, REQ-F-010, REQ-F-022, REQ-NF-007.
#
# "git shared update <repo> [<path>]": una cartella come primo argomento e'
# la repo da aggiornare, come "git -C <repo> shared update".
# "git shared update --peers [<path>]": questa repo e ogni peer dei suoi
# file, in parallelo, con il commit; nei peer solo i casi sicuri.

. "$(dirname "$0")/lib.sh"

# tip <repo>: refs/shared/<id> della repo
tip() {
	git -C "$TRASH/$1" rev-parse -q --verify "refs/shared/$ID"
}

# subject <repo>: oggetto dell'ultimo commit
subject() {
	git -C "$TRASH/$1" log -1 --format=%s
}

# line1 <repo>: prima riga di p.h
line1() {
	head -1 "$TRASH/$1/p.h"
}

# edit <riga 1>: p.h di tre righe, committato nella repo corrente
edit() {
	printf '%s\nl2\nl3\n' "$1" >p.h
	git add p.h
	git commit -q -m "p.h: $1"
}

# publish_a <riga 1>: modifica e pubblica p.h in A
publish_a() {
	cd "$TRASH/A" || exit 1
	edit "$1"
	git shared publish >/dev/null
}

make_repo A
cd "$TRASH/A" || exit 1
edit a0
git shared set -c p.h >/dev/null
ID=$(shared_id p.h)
for R in B C D E; do
	make_repo "$R"
	cd "$TRASH/$R" || exit 1
	put o.txt o
	git shared link -c ../A/p.h >/dev/null
done
FIRST_B=$(git -C "$TRASH/B" rev-list --max-parents=0 HEAD)

# --- update <repo> [<path>]: senza entrare nella repo -----------------------------------
publish_a a1
run git shared update ../B
expect_rc "update <repo>: codice 0" 0
expect_out "update <repo>: applicata in B" "p.h: new version applied. Commit it to confirm."
expect_eq "update <repo>: file di B" "$(line1 B)" "a1"
expect_eq "update <repo>: nessun commit senza -c" "$(subject B)" "Link p.h"
expect_eq "update <repo>: A non cambia" "$(subject A)" "p.h: a1"
git -C "$TRASH/B" commit -q -m "B: a1"
run git shared update -m "C da fuori" ../C p.h
expect_rc "update <repo> <path>: codice 0" 0
expect_out "update <repo> <path>: committata" "p.h: new version applied and committed."
expect_eq "update <repo> <path>: messaggio" "$(subject C)" "C da fuori"
mkdir -p "$TRASH/D/sub"
run git shared update ../D/sub ../p.h
expect_rc "update <sottocartella> <path relativo a lei>: codice 0" 0
expect_eq "update <sottocartella>: file di D" "$(line1 D)" "a1"
git -C "$TRASH/D" commit -q -m "D: a1"
run git shared update ../B p.h altro
expect_rc "update <repo> <path> altro: uso" 129
expect_err "update <repo> <path> altro: messaggio" "too many arguments"
run git shared update ../nessuna
expect_rc "update <non cartella>: e' un path" 1
expect_err "update <non cartella>: non shared" "../nessuna"
mkdir -p "$TRASH/vuota"
run git shared update ../vuota
expect_rc "update <cartella fuori da una repo>: fatale" 128
expect_err "update <cartella fuori da una repo>: messaggio" "not a git repository"
run git shared update -h
expect_out "uso: <repo>" "[<repo>] [<path>]"
expect_out "uso: --peers" "--peers           also update and commit every local peer"

# --- --peers: tutti i peer, in parallelo, con il commit -------------------------------
cd "$TRASH/E" && git shared update -c >/dev/null
publish_a a2
T2=$(tip A)
run git shared update --peers
expect_rc "--peers: codice 0" 0
expect_out "--peers: A gia' aggiornata" "p.h: up to date"
for R in B C D E; do
	expect_out "--peers: $R aggiornata" "../$R: p.h: new version applied and committed."
	expect_eq "--peers: file di $R" "$(line1 $R)" "a2"
	expect_eq "--peers: commit in $R" "$(subject $R)" "Update p.h"
	expect_eq "--peers: T di $R" "$(tip $R)" "$T2"
done
expect_no_out "--peers: nei peer niente riga 'received'" "new version received"
expect_eq "--peers: nell'ordine dei peer" \
	"$(printf '%s\n' "$OUT" | sed -n 's|^\.\./\([A-E]\): .*|\1|p' | tr -d '\n')" "BCDE"
run git shared update --peers
expect_rc "--peers ripetuto: codice 0" 0
expect_out "--peers ripetuto: B allineata" "../B: p.h: up to date"

# da una sottocartella: path dei peer relativi alla cartella corrente
publish_a a3
mkdir -p "$TRASH/A/dir"
cd "$TRASH/A/dir" || exit 1
run git shared update --peers -m "allineo tutti"
expect_rc "--peers da una sottocartella: codice 0" 0
expect_out "--peers da una sottocartella: path relativo" "../../B: p.h: new version applied and committed."
expect_eq "--peers -m: messaggio nel peer" "$(subject C)" "allineo tutti"

# un avviso perso: lo riscrive --peers
publish_a a4
rm -rf "$TRASH/B/.git/shared/inbox"
run git shared update --peers
expect_rc "--peers, avviso perso: codice 0" 0
expect_eq "--peers, avviso perso: B aggiornata" "$(line1 B)" "a4"

# --- un peer che chiede di essere lasciato stare ---------------------------------------
git -C "$TRASH/D" config shared.peerUpdate false
publish_a a5
run git shared update --peers
expect_rc "shared.peerUpdate = false: codice 0" 0
expect_out "shared.peerUpdate = false: saltata" "../D: skipped: shared.peerUpdate is false here"
expect_eq "shared.peerUpdate = false: D non cambia" "$(line1 D)" "a4"
expect_eq "shared.peerUpdate = false: gli altri si" "$(line1 C)" "a5"
git -C "$TRASH/D" config shared.peerUpdate no
run git shared update --peers
expect_out "shared.peerUpdate = no: saltata" "../D: skipped: shared.peerUpdate is false here"
git -C "$TRASH/D" config shared.peerUpdate true
run git shared update --peers
expect_out "shared.peerUpdate = true: aggiornata" "../D: p.h: new version applied and committed."

# --- casi che restano a chi lavora nel peer -------------------------------------------
# modifiche locali
printf 'mia\n' >"$TRASH/E/p.h"
publish_a a6
run git shared update --peers
expect_rc "modifiche locali: codice 1" 1
expect_out "modifiche locali: saltata" "../E: p.h: skipped, it has local changes"
expect_eq "modifiche locali: file intatto" "$(line1 E)" "mia"
expect_eq "modifiche locali: gli altri si" "$(line1 B)" "a6"
git -C "$TRASH/E" checkout -q p.h
git -C "$TRASH/E" config shared.peerUpdate false

# divergenza: C ha committato senza pubblicare
cd "$TRASH/C" || exit 1
printf 'a6\nl2\nc3\n' >p.h
git commit -q -a -m "C: modifica sua"
publish_a a7
run git shared update --peers
expect_rc "divergente: codice 1" 1
expect_out "divergente: saltata" "../C: p.h: skipped, diverged: run 'git shared update' there to merge"
expect_eq "divergente: nessuna unione" "$(sed -n 3p "$TRASH/C/p.h")" "c3"
expect_eq "divergente: file di C intatto" "$(line1 C)" "a6"
expect_eq "divergente: nessun commit in C" "$(subject C)" "C: modifica sua"
cd "$TRASH/C" && git shared update -c >/dev/null 2>&1 && git shared publish >/dev/null 2>&1
git -C "$TRASH/C" config shared.peerUpdate false
cd "$TRASH/A" && git shared update -c >/dev/null 2>&1

# merge di git in corso, HEAD staccata, file sospeso, unione di git shared in corso
publish_a a8
git -C "$TRASH/B" rev-parse HEAD >"$TRASH/B/.git/MERGE_HEAD"
run git shared update --peers
expect_rc "merge in corso: codice 1" 1
expect_out "merge in corso: saltata" "../B: skipped: a merge, rebase, cherry-pick or revert is in progress"
expect_eq "merge in corso: file intatto" "$(line1 B)" "a7"
rm -f "$TRASH/B/.git/MERGE_HEAD"
mkdir "$TRASH/B/.git/rebase-merge"
run git shared update --peers
expect_out "rebase in corso: saltata" "../B: skipped: a merge, rebase, cherry-pick or revert is in progress"
rmdir "$TRASH/B/.git/rebase-merge"
git -C "$TRASH/B" checkout -q --detach
run git shared update --peers
expect_rc "HEAD staccata: codice 1" 1
expect_out "HEAD staccata: saltata" "../B: skipped: HEAD is detached"
git -C "$TRASH/B" checkout -q -b vecchio "$FIRST_B"
run git shared update --peers
expect_out "file non dichiarato sul branch: saltato" "../B: $ID: skipped, suspended on this branch"
expect_ok "file sospeso: nessun p.h sul branch" test ! -f "$TRASH/B/p.h"
git -C "$TRASH/B" checkout -q main 2>/dev/null # avviso dell'hook: lo verifica t0006
git -C "$TRASH/B" update-ref "refs/shared-merge/$ID" HEAD
run git shared update --peers
expect_rc "unione in corso: codice 1" 1
expect_out "unione in corso: saltata" "../B: p.h: skipped, merge in progress: finish it there"
git -C "$TRASH/B" update-ref -d "refs/shared-merge/$ID"
run git shared update --peers
expect_rc "tutto a posto: codice 0" 0
expect_eq "tutto a posto: B aggiornata" "$(line1 B)" "a8"

# --- --peers <path>: solo i peer di quel file -----------------------------------------
cd "$TRASH/A" || exit 1
put q.h q0
git shared set -c q.h >/dev/null
cd "$TRASH/B" && git shared link -c ../A/q.h >/dev/null
cd "$TRASH/A" || exit 1
put q.h q1
git shared publish q.h >/dev/null
publish_a a9
run git shared update --peers q.h
expect_rc "--peers <path>: codice 0" 0
expect_out "--peers <path>: B aggiornata" "../B: q.h: new version applied and committed."
expect_no_out "--peers <path>: p.h non si tocca" "p.h"
expect_no_out "--peers <path>: D non condivide q.h" "../D"
expect_eq "--peers <path>: p.h di D indietro" "$(line1 D)" "a8"

# --- un file non pubblicato qui, un peer sparito, nessun peer ------------------------
cd "$TRASH/A" || exit 1
edit a10
run git shared update --peers p.h
expect_err "non pubblicato: avviso" "p.h: the peers get the last published version, not your local change: run 'git shared publish' first"
git shared publish >/dev/null
mv "$TRASH/D" "$TRASH/D.off"
run git shared update --peers p.h
expect_rc "peer sparito: codice 0" 0
expect_err "peer sparito: irraggiungibile" "not reachable"
expect_no_out "peer sparito: nessun figlio" "../D"
mv "$TRASH/D.off" "$TRASH/D"
make_repo F
cd "$TRASH/F" || exit 1
put s.h s
git shared set -c s.h >/dev/null
run git shared update --peers
expect_rc "nessun peer: codice 0" 0
expect_out "nessun peer: messaggio" "No local peers to update."

# --- opzioni che non vanno con --peers ------------------------------------------------
cd "$TRASH/A" || exit 1
run git shared update --peers -f
expect_rc "--peers -f: uso" 129
expect_err "--peers -f: messaggio" "--peers cannot be used with -f or with a repository"
run git shared update --peers ../B
expect_rc "--peers <repo>: uso" 129
run git shared update --peers --abort
expect_rc "--peers --abort: uso" 129
run git shared update --peers --ours
expect_rc "--peers --ours: uso" 129

done_testing
