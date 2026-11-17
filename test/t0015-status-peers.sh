#!/bin/sh
# t0015-status-peers.sh - git shared status --peers (REQ-F-012, D-086).

. "$(dirname "$0")/lib.sh"

top() {
	git -C "$TRASH/$1" rev-parse --show-toplevel
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
TOP_A=$(top A)
TOP_B=$(top B)
TOP_C=$(top C)

# --- argomenti ------------------------------------------------------------------------
cd "$TRASH/A" || exit 1
run git shared status --boh
expect_rc "opzione sconosciuta: codice 129" 129
run git shared status x
expect_rc "argomento: codice 129" 129
run git shared status -h
expect_out "uso: --peers" "--peers    also show whether each peer has the same version"
run git shared help
expect_out "manuale: --peers" "--peers also shows, for each peer"
run git shared status
expect_no_out "senza --peers: nessun peer" "peers"

# --- tutti allineati ---------------------------------------------------------------------
run git shared status --peers
expect_rc "--peers: codice 0" 0
expect_out "--peers: prima riga" "    peers  $TOP_B: same version"
expect_out "--peers: righe successive allineate" "           $TOP_C: same version"
expect_eq "--peers: niente scritto nei peer" "$(git -C ../B for-each-ref refs/shared-incoming)" ""

# --- peer indietro: manca l'ultima versione ------------------------------------------------
mv "$TRASH/C" "$TRASH/C.off"
put p.h v2
git shared publish >/dev/null 2>&1
run git shared status --peers
expect_out "irraggiungibile perche' spostato: non trovato" "$TOP_C: not found: if it was deleted, run 'git shared prune'"
mv "$TRASH/C.off" "$TRASH/C"
run git shared status --peers
expect_out "indietro: publish" "$TOP_C: older version: run 'git shared publish'"
expect_out "indietro: B allineato" "$TOP_B: same version"
git shared publish >/dev/null 2>&1
run git shared status --peers
expect_out "dopo publish: C allineato" "$TOP_C: same version"

# --- peer avanti: ha una versione che qui non c'e' --------------------------------------
mv "$TRASH/A" "$TRASH/A.off"
cd "$TRASH/B" || exit 1
git shared update -c >/dev/null 2>&1
put p.h v3
git shared publish >/dev/null 2>&1
mv "$TRASH/A.off" "$TRASH/A"
cd "$TRASH/A" || exit 1
run git shared status --peers
expect_out "avanti: update" "$TOP_B: newer or different version: run 'git shared update'"
git shared update -c >/dev/null 2>&1
run git shared status --peers
expect_out "dopo update: B allineato" "$TOP_B: same version"
expect_out "dopo update: C ha la versione di B" "$TOP_C: same version"

# --- divergenza tra peer: versione diversa ---------------------------------------------------
mv "$TRASH/A" "$TRASH/A.off"
cd "$TRASH/C" || exit 1
git shared update -c >/dev/null 2>&1
put p.h v4-c
git shared publish >/dev/null 2>&1
mv "$TRASH/A.off" "$TRASH/A"
cd "$TRASH/A" || exit 1
printf 'v4-a\n' >p.h
git commit -q -a -m "v4-a"
git shared publish >/dev/null 2>&1
git -C ../C fetch -q "$TOP_A" "+refs/shared/$(shared_id p.h):refs/tmp-a" 2>/dev/null
git fetch -q "$TOP_C" "+refs/shared/$(shared_id p.h):refs/tmp-c" 2>/dev/null
run git shared status --peers
expect_out "divergente (oggetto presente): update" "$TOP_C: newer or different version: run 'git shared update'"

# --- peer uscito, nessuna versione, illeggibile -----------------------------------------------
group D E F G
ID=$(cd "$TRASH/D" && shared_id p.h)
mv "$TRASH/D" "$TRASH/D.off"
cd "$TRASH/E" || exit 1
git shared unlink -c p.h >/dev/null 2>&1
mv "$TRASH/D.off" "$TRASH/D"
git -C "$TRASH/F" update-ref -d "refs/shared/$ID"
TOP_G=$(top G)
cp "$TRASH/G/.git/config" "$TRASH/G.config"
printf 'non e[ un config valido\n' >"$TRASH/G/.git/config"
cd "$TRASH/D" || exit 1
run git shared status --peers
expect_out "uscito: prune" "$(top E): no longer shares the file: run 'git shared prune'"
expect_out "senza versione: publish" "$(top F): no version yet: run 'git shared publish'"
expect_out "illeggibile" "$TOP_G: cannot be read now"
cp "$TRASH/G.config" "$TRASH/G/.git/config"

# --- nessun peer, file in attesa --------------------------------------------------------------
make_repo S
cd "$TRASH/S" || exit 1
put o.txt o
put s.h s
git shared set -c s.h >/dev/null
put n.h n
git shared set n.h >/dev/null
run git shared status --peers
expect_out "senza peer: (none)" "    peers  (none)"
expect_eq "in attesa: nessuna riga peers per n.h" "$(git shared status --peers | grep -c peers)" "1"

done_testing
