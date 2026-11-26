#!/bin/sh
# t0029-update-peers-veloce.sh - percorso veloce di update --peers (D-116).
# REQ-F-009, REQ-NF-007.
#
# Il padre legge lo stato dei peer da file e da un solo cat-file, scrive
# oggetti, refs/shared/<id> e un tree sintetico, e usa git solo per status,
# restore e commit. Tutto il resto ripiega sul figlio di D-115.

. "$(dirname "$0")/lib.sh"

tip() {
	git -C "$TRASH/$1" rev-parse -q --verify "refs/shared/$ID"
}

subject() {
	git -C "$TRASH/$1" log -1 --format=%s
}

line1() {
	head -1 "$TRASH/$1/p.h"
}

edit() {
	printf '%s\nl2\nl3\n' "$1" >p.h
	git add p.h
	git commit -q -m "p.h: $1"
}

publish_a() {
	cd "$TRASH/A" || exit 1
	edit "$1"
	git shared publish >/dev/null
}

# peer_procs <file di trace2>: processi git avviati in un peer (con -C <peer>
# o un figlio --as-peer), escluso cio' che riguarda solo A
peer_procs() {
	grep '"event":"start"' "$1" | grep -c '"-C","[^"]*/[B-E]"'
}

# as_peer <file di trace2>: figli di ripiego avviati. Il figlio e' git-shared
# lanciato senza git, e trace2 non lo vede: si contano i suoi symbolic-ref
# (uno per figlio, piu' quello del padre).
as_peer() {
	echo $(( $(grep '"event":"start"' "$1" | grep -c '"symbolic-ref"') - 1 ))
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

# --- da applicare: 3 processi per peer, nessun figlio ------------------------------------
publish_a a1
T1=$(tip A)
GIT_TRACE2_EVENT="$TRASH/tr1" git shared update --peers >"$TRASH/.out" 2>"$TRASH/.err"
RC=$?
OUT=$(cat "$TRASH/.out")
expect_rc "veloce: codice 0" 0
for R in B C D E; do
	expect_out "veloce: $R aggiornata" "../$R: p.h: new version applied and committed."
	expect_eq "veloce: file di $R" "$(line1 $R)" "a1"
	expect_eq "veloce: T di $R" "$(tip $R)" "$T1"
	expect_eq "veloce: commit in $R" "$(subject $R)" "Update p.h"
done
expect_eq "veloce: nessun figlio di ripiego" "$(as_peer "$TRASH/tr1")" 0
expect_eq "veloce: 3 processi per peer" "$(peer_procs "$TRASH/tr1")" 12
expect_eq "veloce: B pulita dopo" "$(git -C "$TRASH/B" status --porcelain)" ""
expect_ok "veloce: fsck di B" git -C "$TRASH/B" fsck --no-progress
expect_ok "veloce: avviso di T tolto" test ! -f "$TRASH/B/.git/shared/inbox/$ID/$T1"
expect_no_out "veloce: maintenance non lanciata" "maintenance"

# --- gia' allineati: nessun processo nei peer -----------------------------------------
GIT_TRACE2_EVENT="$TRASH/tr2" git shared update --peers >"$TRASH/.out" 2>&1
OUT=$(cat "$TRASH/.out")
expect_out "allineati: B" "../B: p.h: up to date"
expect_eq "allineati: nessun processo nei peer" "$(peer_procs "$TRASH/tr2")" 0

# --- solo la ref: il file e' gia' quello, il peer non ha la versione -----------------
publish_a a2
T2=$(tip A)
printf 'a2\nl2\nl3\n' >"$TRASH/C/p.h"
git -C "$TRASH/C" commit -q -a -m "C: stesso contenuto"
GIT_TRACE2_EVENT="$TRASH/tr3" git shared update --peers >"$TRASH/.out" 2>&1
OUT=$(cat "$TRASH/.out")
expect_out "solo la ref: C allineata" "../C: p.h: up to date"
expect_eq "solo la ref: T di C" "$(tip C)" "$T2"
expect_eq "solo la ref: nessun commit in C" "$(subject C)" "C: stesso contenuto"
expect_eq "solo la ref: nessun processo in C" \
	"$(grep '"event":"start"' "$TRASH/tr3" | grep -c '"-C","[^"]*/C"')" 0

# --- ricevuta ma non applicata (status nel peer): si applica senza oggetti -----------
publish_a a3
(cd "$TRASH/D" && git shared status >/dev/null)
GIT_TRACE2_EVENT="$TRASH/tr4" git shared update --peers >"$TRASH/.out" 2>&1
OUT=$(cat "$TRASH/.out")
expect_out "ricevuta prima: D aggiornata" "../D: p.h: new version applied and committed."
expect_eq "ricevuta prima: nessun figlio" "$(as_peer "$TRASH/tr4")" 0

# --- ref impacchettate e oggetti in un pack ------------------------------------------
git -C "$TRASH/B" pack-refs --all
git -C "$TRASH/B" gc -q
publish_a a4
T4=$(tip A)
GIT_TRACE2_EVENT="$TRASH/tr5" git shared update --peers >"$TRASH/.out" 2>&1
OUT=$(cat "$TRASH/.out")
expect_out "packed-refs e gc: B aggiornata" "../B: p.h: new version applied and committed."
expect_eq "packed-refs e gc: nessun figlio" "$(as_peer "$TRASH/tr5")" 0
expect_eq "packed-refs e gc: T di B" "$(tip B)" "$T4"
expect_ok "packed-refs e gc: fsck di B" git -C "$TRASH/B" fsck --no-progress

# --- fine riga: checkout-index converte come nel peer --------------------------------
git -C "$TRASH/C" config core.autocrlf true
publish_a a5
run git shared update --peers
expect_out "autocrlf: C aggiornata" "../C: p.h: new version applied and committed."
expect_eq "autocrlf: CRLF nel working tree di C" "$(od -c "$TRASH/C/p.h" | grep -c '\\r')" 1
expect_eq "autocrlf: C pulita" "$(git -C "$TRASH/C" status --porcelain)" ""
# autocrlf resta: tolto, il file con CRLF risulterebbe modificato

# --- pre-commit che rifiuta: applicata, da committare --------------------------------
mkdir -p "$TRASH/E/.git/hooks"
printf '#!/bin/sh\necho no >&2\nexit 1\n' >"$TRASH/E/.git/hooks/pre-commit"
chmod +x "$TRASH/E/.git/hooks/pre-commit"
publish_a a6
run git shared update --peers
expect_rc "pre-commit: codice 1" 1
expect_out "pre-commit: commit fallito" "../E: p.h: new version applied, the commit failed: commit it there"
expect_out "pre-commit: l'uscita dell'hook" "../E: no"
expect_eq "pre-commit: file applicato" "$(line1 E)" "a6"
run git -C "$TRASH/E" shared status
expect_out "pre-commit: da committare" "applied, to commit"
rm -f "$TRASH/E/.git/hooks/pre-commit"
git -C "$TRASH/E" commit -q -m "E: a6"

# --- lock gia' preso: nulla cambia ----------------------------------------------------
publish_a a7
OLD_B=$(tip B)
: >"$TRASH/B/.git/refs/shared/$ID.lock"
run git shared update --peers
expect_rc "lock preso: codice 1" 1
expect_out "lock preso: messaggio" "../B: p.h: cannot apply the new version: run 'git shared update' there"
expect_eq "lock preso: T di B invariata" "$(tip B)" "$OLD_B"
expect_eq "lock preso: file di B invariato" "$(line1 B)" "a6"
rm -f "$TRASH/B/.git/refs/shared/$ID.lock"
run git shared update --peers
expect_eq "lock tolto: B aggiornata" "$(line1 B)" "a7"

# --- motivi di ripiego: il figlio fa come prima --------------------------------------
# .gitshared modificato nel working tree
publish_a a8
printf '# nota\n' >>"$TRASH/B/.gitshared"
GIT_TRACE2_EVENT="$TRASH/tr6" git shared update --peers >"$TRASH/.out" 2>&1
OUT=$(cat "$TRASH/.out")
expect_eq ".gitshared modificato: un figlio" "$(as_peer "$TRASH/tr6")" 1
expect_out ".gitshared modificato: B aggiornata dal figlio" "../B: p.h: new version applied and committed."
git -C "$TRASH/B" checkout -q .gitshared
# config con include
printf '[core]\n\tbare = false\n' >"$TRASH/C/.git/extra.cfg"
git -C "$TRASH/C" config include.path extra.cfg
publish_a a9
GIT_TRACE2_EVENT="$TRASH/tr7" git shared update --peers >"$TRASH/.out" 2>&1
OUT=$(cat "$TRASH/.out")
expect_eq "include: un figlio" "$(as_peer "$TRASH/tr7")" 1
expect_out "include: C aggiornata dal figlio" "../C: p.h: new version applied and committed."
git -C "$TRASH/C" config --unset include.path
# un peer con un file divergente va tutto al figlio, che lo salta
cd "$TRASH/D" || exit 1
printf 'a9\nl2\nd3\n' >p.h
git commit -q -a -m "D: modifica sua"
publish_a a10
run git shared update --peers
expect_rc "divergente: codice 1" 1
expect_out "divergente: dal figlio" "../D: p.h: skipped, diverged"
expect_out "divergente: gli altri veloci" "../B: p.h: new version applied and committed."

# --- tree sintetico: sottocartelle, piu' file, ordine dei nomi di git ---------------
# "inc-x.h" (file) e "inc/" (cartella): nel tree "inc-x.h" viene prima, perche'
# una cartella vale "inc/" ('-' < '/'); fsck controlla hash e ordine.
git -C "$TRASH/D" config shared.peerUpdate false # divergente: andrebbe al figlio
cd "$TRASH/A" || exit 1
mkdir -p inc/sub
put inc/sub/q.h q0
put inc-x.h x0
git shared set -c inc/sub/q.h >/dev/null
git shared set -c inc-x.h >/dev/null
cd "$TRASH/E" && git shared link -c ../A/inc/sub/q.h >/dev/null && git shared link -c ../A/inc-x.h >/dev/null
cd "$TRASH/A" || exit 1
put inc/sub/q.h q1
put inc-x.h x1
git shared publish >/dev/null 2>&1
GIT_TRACE2_EVENT="$TRASH/tr8" git shared update --peers >"$TRASH/.out" 2>&1
OUT=$(cat "$TRASH/.out")
expect_out "tree sintetico: q.h in E" "../E: inc/sub/q.h: new version applied and committed."
expect_out "tree sintetico: inc-x.h in E" "../E: inc-x.h: new version applied and committed."
expect_eq "tree sintetico: contenuto di q.h" "$(cat "$TRASH/E/inc/sub/q.h")" "q1"
expect_eq "tree sintetico: contenuto di inc-x.h" "$(cat "$TRASH/E/inc-x.h")" "x1"
expect_eq "tree sintetico: un commit per i due file" "$(subject E)" "Update shared files"
expect_eq "tree sintetico: nessun figlio" "$(as_peer "$TRASH/tr8")" 0
expect_eq "tree sintetico: E pulita" "$(git -C "$TRASH/E" status --porcelain)" ""
expect_ok "tree sintetico: fsck di E (hash e ordine)" git -C "$TRASH/E" fsck --strict --no-progress

# --- SHA-256 ------------------------------------------------------------------------
for R in S1 S2; do
	git init -q -b main --object-format=sha256 "$TRASH/$R"
done
cd "$TRASH/S1" || exit 1
mkdir -p dir
printf 's0
' >dir/s.h
git add dir/s.h && git commit -q -m s0
git shared set -c dir/s.h >/dev/null
cd "$TRASH/S2" && put o.txt o && git shared link -c ../S1/dir/s.h >/dev/null
cd "$TRASH/S1" || exit 1
printf 's1
' >dir/s.h
git commit -q -a -m s1
git shared publish >/dev/null
GIT_TRACE2_EVENT="$TRASH/tr9" git shared update --peers >"$TRASH/.out" 2>&1
OUT=$(cat "$TRASH/.out")
expect_out "sha256: S2 aggiornata" "../S2: dir/s.h: new version applied and committed."
expect_eq "sha256: contenuto" "$(cat "$TRASH/S2/dir/s.h")" "s1"
expect_eq "sha256: nessun figlio" "$(as_peer "$TRASH/tr9")" 0
expect_ok "sha256: fsck di S2" git -C "$TRASH/S2" fsck --strict --no-progress

done_testing
