#!/bin/sh
# t0013-relocate.sh - git shared relocate e rilevamento dello spostamento
# (REQ-F-015, REQ-F-017, D-084; reportIdea B-22).

. "$(dirname "$0")/lib.sh"

top() {
	git -C "$TRASH/$1" rev-parse --show-toplevel
}

# peers_of <repo> <id>: peer della repo, uno per riga, ordinati
peers_of() {
	git -C "$TRASH/$1" config --get-all "shared.$2.peer" | sort
}

sorted() {
	printf '%s\n' "$@" | sort
}

# group <sorgente> <peer...>
group() {
	make_repo "$1"
	cd "$TRASH/$1" || exit 1
	put p.h v1
	git shared set -c p.h >/dev/null
	src=$1
	shift
	for R in "$@"; do
		make_repo "$R"
		cd "$TRASH/$R" || exit 1
		put o.txt o
		git shared link -c "../$src/p.h" >/dev/null
	done
}

group A B C
ID=$(cd "$TRASH/A" && shared_id p.h)
TOP_A=$(top A)
TOP_B=$(top B)
TOP_C=$(top C)

# --- la repo ricorda dove sta ----------------------------------------------------
cd "$TRASH/B" || exit 1
expect_eq "selfPath: scritto" "$(git config shared.selfPath)" "$TOP_B"
run git shared status
expect_eq "selfPath: al suo posto, nessun avviso" "$ERR" ""

# --- argomenti ----------------------------------------------------------------------
run git shared relocate -h
expect_out "uso: breve" "Tell the peers that this repository was moved or copied."
run git shared relocate --help
expect_out "manuale: relocate" "run relocate to give the peers the new path"
run git shared relocate a b
expect_rc "due path senza --peer: codice 129" 129
run git shared relocate --peer a
expect_rc "--peer con un path: codice 129" 129
run git shared relocate --copy --peer a b
expect_rc "--copy con --peer: codice 129" 129
run git shared relocate
expect_rc "non spostata: codice 1" 1
expect_err "non spostata: errore" "this repository is still at $TOP_B"
make_repo empty
cd "$TRASH/empty" || exit 1
run git shared relocate ../x
expect_rc "nessun file collegato: codice 1" 1
expect_err "nessun file collegato: errore" "no file is linked here"

# --- spostamento ----------------------------------------------------------------------
mv "$TRASH/B" "$TRASH/B2"
TOP_B2=$(top B2)
cd "$TRASH/A" || exit 1
put p.h v2
run git shared publish
expect_err "spostata: i peer non la trovano" "peer $TOP_B not reachable"
cd "$TRASH/B2" || exit 1
run git shared status
expect_err "spostata: avviso con il comando" "this repository was at $TOP_B: if it was moved, run 'git shared relocate'"
run git shared relocate
expect_rc "relocate: codice 0" 0
expect_out "relocate: peer avvisati" "p.h: 2 of 2 peers know the new path"
expect_out "relocate: nuovo path" "This repository is now known at $TOP_B2."
expect_eq "relocate: A conosce il nuovo path" "$(peers_of A "$ID")" "$(sorted "$TOP_B2" "$TOP_C")"
expect_eq "relocate: C conosce il nuovo path" "$(peers_of C "$ID")" "$(sorted "$TOP_A" "$TOP_B2")"
expect_eq "relocate: selfPath aggiornato" "$(git config shared.selfPath)" "$TOP_B2"
run git shared status
expect_eq "relocate: nessun avviso dopo" "$ERR" ""
run git shared update -c
expect_out "relocate: update funziona" "new version applied and committed"
cd "$TRASH/A" || exit 1
put p.h v3
run git shared publish
expect_out "relocate: publish raggiunge tutti" "sent to 2 of 2 peers"

# Ripetuto con il vecchio path esplicito, relativo: nessun doppione.
cd "$TRASH/B2" || exit 1
run git shared relocate ../B
expect_rc "ripetuto: codice 0" 0
expect_eq "ripetuto: nessun doppione in A" "$(peers_of A "$ID")" "$(sorted "$TOP_B2" "$TOP_C")"

# --- copia (B-22) -------------------------------------------------------------------
cp -r "$TRASH/C" "$TRASH/C2"
TOP_C2=$(top C2)
cd "$TRASH/C2" || exit 1
run git shared status
expect_err "copia: avviso" "if it is a copy, 'git shared relocate --copy'"
run git shared relocate
expect_rc "copia senza --copy: rifiutata" 1
expect_err "copia senza --copy: perche'" "$TOP_C still contains a repository: if this one is a copy, run 'git shared relocate --copy $TOP_C'"
expect_eq "copia senza --copy: A non toccata" "$(peers_of A "$ID")" "$(sorted "$TOP_B2" "$TOP_C")"
run git shared relocate --copy
expect_rc "--copy: codice 0" 0
expect_out "--copy: condiviso anche da qui" "p.h: shared from here too (3 of 3 peers told)"
expect_eq "--copy: A conosce entrambe" "$(peers_of A "$ID")" "$(sorted "$TOP_B2" "$TOP_C" "$TOP_C2")"
expect_eq "--copy: C conosce la copia" "$(peers_of C "$ID")" "$(sorted "$TOP_A" "$TOP_B2" "$TOP_C2")"
expect_eq "--copy: la copia conosce C" "$(peers_of C2 "$ID")" "$(sorted "$TOP_A" "$TOP_B2" "$TOP_C")"
run git shared status
expect_eq "--copy: nessun avviso dopo" "$ERR" ""
cd "$TRASH/A" || exit 1
put p.h v4
run git shared publish
expect_out "--copy: publish a tutte" "sent to 3 of 3 peers"

cd "$TRASH/B2" || exit 1
run git shared relocate --copy ../B
expect_rc "--copy di una repo sparita: rifiutato" 1
expect_err "--copy di una repo sparita: perche'" "no repository at $TOP_B anymore"

# --- --peer: un peer spostato che non l'ha detto -----------------------------------
group E F
ID=$(cd "$TRASH/E" && shared_id p.h)
TOP_F=$(top F)
mv "$TRASH/F" "$TRASH/F2"
TOP_F2=$(top F2)
cd "$TRASH/E" || exit 1
run git shared relocate --peer ../F ../F2
expect_rc "--peer: codice 0" 0
expect_out "--peer: messaggio" "p.h: peer $TOP_F is now $TOP_F2"
expect_eq "--peer: elenco aggiornato" "$(peers_of E "$ID")" "$TOP_F2"
put p.h v2
run git shared publish
expect_out "--peer: publish raggiunge il peer" "sent to 1 of 1 peers"
run git shared relocate --peer ../F ../F2
expect_rc "--peer ripetuto: nessun peer con quel path" 1
expect_err "--peer ripetuto: errore" "no shared file here has $TOP_F as a peer"
run git shared relocate --peer ../F2 ../A
expect_rc "--peer con il vecchio path ancora presente: rifiutato" 1
expect_err "--peer: non spostato" "still contains a repository: it was not moved"
mv "$TRASH/F2" "$TRASH/F3"
run git shared relocate --peer ../F2 ../empty
expect_rc "--peer verso una repo che non condivide: codice 1" 1
expect_err "--peer verso una repo che non condivide: avviso" "does not share this file: skipped"

# --- worktree secondario: nessun avviso ------------------------------------------------
cd "$TRASH/A" || exit 1
git worktree add -q "$TRASH/W" -b wt 2>/dev/null
cd "$TRASH/W" || exit 1
run git shared status
expect_eq "worktree: nessun avviso di spostamento" "$ERR" ""
expect_eq "worktree: selfPath della repo principale" "$(git config shared.selfPath)" "$TOP_A"

done_testing
