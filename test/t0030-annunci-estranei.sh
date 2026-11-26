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
# resta una scelta dell'utente (D-117). Per non ricevere piu' gli avvisi,
# 'git shared prune --one-way' in A (D-118).

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
TOP_C=$(top C)

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
	"note   new version announced by $TOP_A, which is not a peer. There, 'git shared update --peers' brings it here; 'git shared prune --one-way' stops the notices"
expect_no_out "status: non manda a update" "not received: run 'git shared update'"

run git shared update
expect_rc "update: codice 0" 0
expect_out "update: chi lo ha annunciato e come riceverla" \
	"p.h: new version announced by $TOP_A, which is not a peer of this repository. There, 'git shared update --peers' brings it here; 'git shared prune --one-way' stops the notices"
expect_eq "update: niente ricevuto" "$(tip C)" "$OLD"
expect_eq "update: l'avviso resta" "$(notices C)" 1
expect_eq "update: C resta senza peer" "$(git config --get-all "shared.$ID.peer")" ""

# Hook: dopo un checkout, lo stesso suggerimento e non "run 'git shared update'".
git checkout -q -b altro 2>/dev/null
run git checkout -q main
expect_err "hook: come riceverla" \
	"warning: p.h: new version announced by $TOP_A, which is not a peer of this repository. There, 'git shared update --peers' brings it here; 'git shared prune --one-way' stops the notices"
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

# --- prune --one-way: smettere di avvisare chi non elenca questa repo (D-118) ---------
# C torna senza peer; A e B lo elencano ancora.
git -C "$TRASH/C" config --unset-all "shared.$ID.peer"
cd "$TRASH/A" || exit 1
edit a3
git shared publish >/dev/null 2>&1
expect_eq "one-way: avviso di A in C" "$(notices C)" 1

# Senza l'opzione C resta: anche un peer che ha perso l'annuncio di un link
# non elenca la repo nuova (A-13).
run git shared prune
expect_out "prune senza opzione: C resta" "Nothing to prune."
expect_ok "prune senza opzione: A elenca C" git config --fixed-value --get "shared.$ID.peer" "$TOP_C"

run git shared prune -n --one-way
expect_rc "prune -n --one-way: codice 0" 0
expect_out "prune -n --one-way: lo dice" "p.h: would remove peer $TOP_C (it does not list this repository)"
expect_ok "prune -n --one-way: A elenca ancora C" git config --fixed-value --get "shared.$ID.peer" "$TOP_C"
expect_eq "prune -n --one-way: l'avviso resta" "$(notices C)" 1

run git shared prune --one-way
expect_rc "prune --one-way: codice 0" 0
expect_out "prune --one-way: C tolto" "p.h: removed peer $TOP_C (it does not list this repository)"
expect_fail "prune --one-way: A non elenca C" git config --fixed-value --get "shared.$ID.peer" "$TOP_C"
expect_fail "prune --one-way: neanche B (C non elenca B)" \
	git -C "$TRASH/B" config --fixed-value --get "shared.$ID.peer" "$TOP_C"
expect_ok "prune --one-way: B resta peer di A" git config --fixed-value --get "shared.$ID.peer" "$TOP_B"
expect_eq "prune --one-way: avviso ritirato da C" "$(notices C)" 0
cd "$TRASH/C" || exit 1
run git shared status
expect_no_out "prune --one-way: in C nessuna nota" "note"

cd "$TRASH/A" || exit 1
edit a4
run git shared publish
expect_out "dopo prune --one-way: avvisato solo B" "p.h: 1 of 1 peers notified"
expect_eq "dopo prune --one-way: niente in C" "$(notices C)" 0
cd "$TRASH/B" && git shared update -c >/dev/null 2>&1

# C elenca B ma non A: --one-way in A toglie C solo da A; B lo tiene, e i
# suoi avvisi in C restano.
git -C "$TRASH/A" config --add "shared.$ID.peer" "$TOP_C"
git -C "$TRASH/B" config --add "shared.$ID.peer" "$TOP_C"
git -C "$TRASH/C" config --add "shared.$ID.peer" "$TOP_B"
cd "$TRASH/A" || exit 1
edit a5
git shared publish >/dev/null 2>&1
FAKE=$(git -C "$TRASH/A" rev-parse HEAD)
printf '%s\n' "$TOP_B" >"$TRASH/C/.git/shared/inbox/$ID/$FAKE"
run git shared prune --one-way
expect_out "solo da A: C tolto" "p.h: removed peer $TOP_C (it does not list this repository)"
expect_ok "solo da A: B elenca ancora C" \
	git -C "$TRASH/B" config --fixed-value --get "shared.$ID.peer" "$TOP_C"
expect_fail "solo da A: avviso di A ritirato" test -f "$TRASH/C/.git/shared/inbox/$ID/$(tip A)"
expect_ok "solo da A: avviso di B resta" test -f "$TRASH/C/.git/shared/inbox/$ID/$FAKE"
rm -f "$TRASH/C/.git/shared/inbox/$ID/$FAKE"

# --- prune in C: avvisi rimasti di chi non lo elenca piu' ---------------------------
# Un avviso di A (non peer di C) scritto prima che A dimenticasse C.
printf '%s\n' "$TOP_A" >"$TRASH/C/.git/shared/inbox/$ID/$FAKE"
git -C "$TRASH/A" config --add "shared.$ID.peer" "$TOP_C"
cd "$TRASH/C" || exit 1
run git shared prune
expect_out "prune in C: A elenca C, l'avviso resta" "Nothing to prune."
expect_ok "prune in C: avviso di A ancora li'" test -f "$TRASH/C/.git/shared/inbox/$ID/$FAKE"
git -C "$TRASH/A" config --fixed-value --unset-all "shared.$ID.peer" "$TOP_C"
run git shared prune -n
expect_out "prune -n in C: lo dice" \
	"p.h: would remove the notices of $TOP_A (not a peer, and it does not list this repository)"
expect_ok "prune -n in C: non tocca" test -f "$TRASH/C/.git/shared/inbox/$ID/$FAKE"
run git shared prune
expect_rc "prune in C: codice 0" 0
expect_out "prune in C: avvisi tolti" \
	"p.h: removed the notices of $TOP_A (not a peer, and it does not list this repository)"
expect_fail "prune in C: avviso tolto" test -f "$TRASH/C/.git/shared/inbox/$ID/$FAKE"
expect_ok "prune in C: B resta peer di C" git config --fixed-value --get "shared.$ID.peer" "$TOP_B"

done_testing
