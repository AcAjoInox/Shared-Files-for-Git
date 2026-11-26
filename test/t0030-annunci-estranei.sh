#!/bin/sh
# t0030-annunci-estranei.sh - avviso di una repo che non e' un peer.
# REQ-F-009, REQ-F-010, REQ-F-013.
#
# Le liste dei peer possono non essere simmetriche: A elenca C, C non elenca
# A (per esempio C e' collegato solo in remoto, per scelta). A pubblica e
# scrive l'avviso in C; update in C non scarica da una repo che non e' un
# suo peer. Prima status diceva "run 'git shared update'" e update "up to
# date", in un giro senza uscita. Ora update, status e l'hook dicono come
# riceverla: 'git shared update --peers' lanciato da A. Entrare tra i peer
# resta una scelta dell'utente.

. "$(dirname "$0")/lib.sh"

top() {
	git -C "$TRASH/$1" rev-parse --show-toplevel
}

# tip <repo>: refs/shared/<id> della repo
tip() {
	git -C "$TRASH/$1" rev-parse -q --verify "refs/shared/$ID"
}

# notices <repo>: quanti avvisi ci sono
notices() {
	ls "$TRASH/$1/.git/shared/inbox/$ID" 2>/dev/null | wc -l | tr -d ' '
}

# edit <riga 1>: p.h di tre righe, committato nella repo corrente
edit() {
	printf '%s\nl2\nl3\n' "$1" >p.h
	git add p.h
	git commit -q -m "p.h: $1"
}

make_repo A
cd "$TRASH/A" || exit 1
edit l1
git shared set -c p.h >/dev/null
ID=$(shared_id p.h)
for R in B C; do
	make_repo "$R"
	cd "$TRASH/$R" || exit 1
	put o.txt o
	git shared link -c ../A/p.h >/dev/null
done
TOP_A=$(top A)
TOP_B=$(top B)

# C non elenca nessun peer: A e B continuano a elencare C.
git -C "$TRASH/C" config --unset-all "shared.$ID.peer"

# --- C senza peer: l'avviso di A non arriva -------------------------------------------
cd "$TRASH/A" || exit 1
edit a1
git shared publish >/dev/null 2>&1
cd "$TRASH/C" || exit 1
OLD=$(tip C)
expect_eq "avviso: arrivato in C" "$(notices C)" 1

run git shared status
expect_rc "status: codice 0" 0
expect_out "status: chi lo ha annunciato e come riceverla" \
	"note   new version announced by $TOP_A, which is not a peer: to receive it, run 'git shared update --peers' in $TOP_A"
expect_no_out "status: non manda a update" "not received: run 'git shared update'"

run git shared update
expect_rc "update: codice 0" 0
expect_out "update: chi lo ha annunciato e come riceverla" \
	"p.h: new version announced by $TOP_A, which is not a peer of this repository: to receive it, run 'git shared update --peers' in $TOP_A"
expect_eq "update: niente ricevuto" "$(tip C)" "$OLD"
expect_eq "update: l'avviso resta" "$(notices C)" 1
expect_eq "update: C resta senza peer" "$(git config --get-all "shared.$ID.peer")" ""

# Hook: dopo un checkout, lo stesso suggerimento e non "run 'git shared update'".
git checkout -q -b altro 2>/dev/null
run git checkout -q main
expect_err "hook: come riceverla" \
	"warning: p.h: new version announced by $TOP_A, which is not a peer of this repository: to receive it, run 'git shared update --peers' in $TOP_A"
case "$ERR" in
*"announced by a peer (run 'git shared update')"*)
	not_ok "hook: non manda a update" "stderr: $ERR" ;;
*) ok "hook: non manda a update" ;;
esac

# --- l'utente sceglie: update --peers da A porta la versione in C ----------------------
cd "$TRASH/A" || exit 1
run git shared update --peers
expect_rc "update --peers da A: codice 0" 0
expect_eq "update --peers da A: C ha la versione" "$(tip C)" "$(tip A)"
expect_eq "update --peers da A: avviso consumato" "$(notices C)" 0
cd "$TRASH/C" || exit 1
run git shared status
expect_no_out "status dopo: nessuna nota" "note"
expect_eq "status dopo: C resta senza peer" "$(git config --get-all "shared.$ID.peer")" ""

# --- C con un peer che non ha ancora la versione, poi ce l'ha --------------------------
# A non e' peer di C: C cerca nei suoi peer (B). Finche' B non l'ha, il
# suggerimento; quando B l'ha ricevuta, C la prende da B.
git config --add "shared.$ID.peer" "$TOP_B"
cd "$TRASH/A" || exit 1
edit a2
git shared publish >/dev/null 2>&1
cd "$TRASH/C" || exit 1
OLD=$(tip C)
run git shared update
expect_out "peer senza la versione: suggerimento" \
	"p.h: new version announced by $TOP_A, which is not a peer of this repository"
expect_eq "peer senza la versione: niente ricevuto" "$(tip C)" "$OLD"
cd "$TRASH/B" || exit 1
git shared update -c >/dev/null 2>&1
cd "$TRASH/C" || exit 1
run git shared update -c
expect_rc "peer con la versione: codice 0" 0
expect_eq "peer con la versione: C la riceve da B" "$(tip C)" "$(tip A)"
expect_no_out "peer con la versione: nessun suggerimento" "which is not a peer"
expect_eq "peer con la versione: avviso consumato" "$(notices C)" 0

done_testing
