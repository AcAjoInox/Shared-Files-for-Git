#!/bin/sh
# t0032-log.sh - git shared log (REQ-F-032, D-120): le versioni della
# catena, il grafo con le unioni, i segni latest e this branch, -n,
# --porcelain e -z, la scelta del file, gli errori. I cambi di creatore,
# di autorita' e di eredi sono provati in t0026 e t0027, accanto agli
# scenari che li creano.

. "$(dirname "$0")/lib.sh"

tip() {
	git -C "$TRASH/$1" rev-parse -q --verify "refs/shared/$ID"
}
abbrev() {
	git -C "$TRASH/$1" rev-parse --short "$2"
}
# porc_lines <tipo>: le righe di $OUT di quel tipo
porc_lines() {
	printf '%s\n' "$OUT" | grep "^$1 "
}

make_repo A
cd "$TRASH/A" || exit 1
put o.txt o

# --- argomenti, nessun file ------------------------------------------------------------
run git shared log -h
expect_out "uso" "usage: git shared log [-n <number>] [--porcelain] [-z] [<path>]"
run git shared log --help
expect_out "manuale" "GIT-SHARED-LOG(1)"
run git shared log --boh
expect_rc "opzione sconosciuta: codice 129" 129
run git shared log -n 0
expect_rc "-n 0: codice 129" 129
run git shared log -n
expect_rc "-n senza valore: codice 129" 129
run git shared log a b
expect_rc "due argomenti: codice 129" 129
run git shared log
expect_rc "nessun file: codice 1" 1
expect_err "nessun file: lo dice" "no shared file here"

# --- una versione ------------------------------------------------------------------------
printf 'a\nb\nc\n' >p.h
git add p.h
git commit -q -m p.h
git shared set -c p.h >/dev/null
ID=$(shared_id p.h)
V1=$(tip A)
run git shared log
expect_rc "una versione: codice 0" 0
expect_out "una versione: riga" "* $(abbrev A "$V1") "
expect_out "una versione: segni" "Test Author (latest, this branch): first version"
run git shared log p.h
expect_out "per path" "first version"
run git shared log "$ID"
expect_out "per id" "first version"
run git shared log x.h
expect_rc "file non shared: codice 1" 1
expect_err "file non shared: lo dice" "'x.h' is not a shared file"

# --- due peer pubblicano senza vedersi: unione -------------------------------------------
make_repo B
cd "$TRASH/B" || exit 1
put o.txt o
git shared link -c ../A/p.h >/dev/null
cd "$TRASH/A" || exit 1
printf 'A\nb\nc\n' >p.h
git commit -q -a -m A
git shared publish >/dev/null 2>&1
VA=$(tip A)
cd "$TRASH/B" || exit 1
mv "$TRASH/A" "$TRASH/A.off"
printf 'a\nb\nC\n' >p.h
git commit -q -a -m C
git shared publish >/dev/null 2>&1
VB=$(tip B)
mv "$TRASH/A.off" "$TRASH/A"
git shared update -c >/dev/null 2>&1
git shared publish >/dev/null 2>&1
VM=$(tip B)
expect_eq "unione: due genitori" "$(git rev-list --parents -n 1 "$VM" | wc -w | tr -d ' ')" "3"
run git shared log
expect_out "unione: segnata" "$(abbrev B "$VM") "
expect_out "unione: latest e merge" "(latest, this branch): merge"
expect_out "unione: il grafo di git" "|\\"
expect_out "unione: versione di A" "$(abbrev B "$VA") "
expect_out "unione: versione di B" "$(abbrev B "$VB") "
expect_out "unione: la prima" "$(abbrev B "$V1") "

# il file cambiato qui: la base resta, con "changed"
printf 'a\nb\nC\nd\n' >p.h
git commit -q -a -m d
run git shared log
expect_out "modificato qui" "(latest, this branch, changed): merge"

# -n
run git shared log -n 1
expect_out "-n 1: l'ultima" "$(abbrev B "$VM") "
expect_no_out "-n 1: non la prima" "first version"

# --- porcelain -----------------------------------------------------------------------------
TOP_B=$(git rev-parse --show-toplevel)
run git shared log --porcelain
expect_rc "porcelain: codice 0" 0
expect_eq "porcelain: intestazione" "$(printf '%s\n' "$OUT" | sed -n 1,3p)" "# porcelain 1
# id $ID
# path p.h"
expect_out "porcelain: versione con blob, data, autore" \
	"version $VM $(git rev-parse "$VM:content") $(git log -1 --format=%at "$VM") Test Author"
expect_eq "porcelain: genitori dell'unione" "$(porc_lines "parent $VM")" "parent $VM $VB
parent $VM $VA"
expect_out "porcelain: latest" "latest $VM"
expect_out "porcelain: base cambiata" "base $VM changed"
expect_eq "porcelain: quattro versioni" "$(porc_lines version | wc -l | tr -d ' ')" "4"
expect_no_out "porcelain: niente grafo" "|"
expect_no_out "porcelain: niente frasi" "first version"
git shared log --porcelain >"$TRASH/p.out"
expect_eq "porcelain: solo LF" "$(tr -cd '\r' <"$TRASH/p.out" | wc -c | tr -d ' ')" "0"
git shared log -z >"$TRASH/z.out"
expect_eq "-z: nessun a capo" "$(tr -cd '\n' <"$TRASH/z.out" | wc -c | tr -d ' ')" "0"
expect_eq "-z: NUL come fine riga" "$(tr '\0' '\n' <"$TRASH/z.out")" "$(cat "$TRASH/p.out")"

# --- prima si riceve, come status --------------------------------------------------------
cd "$TRASH/A" || exit 1
git shared update -c >/dev/null 2>&1
printf 'A\nb\nC\nx\n' >p.h
git commit -q -a -m x
git shared publish >/dev/null 2>&1
VX=$(tip A)
cd "$TRASH/B" || exit 1
run git shared log -n 1
expect_out "riceve prima: la versione annunciata" "$(abbrev B "$VX") "
expect_eq "riceve prima: T avanzata" "$(tip B)" "$VX"

# --- piu' file: quale -----------------------------------------------------------------------
put q.h q
git shared set -c q.h >/dev/null
run git shared log
expect_rc "piu' file: codice 1" 1
expect_err "piu' file: chiede quale" "more than one shared file"
run git shared log q.h
expect_out "piu' file: q.h" "first version"

# --- non collegato (un clone) --------------------------------------------------------------
git clone -q "$TRASH/A" "$TRASH/K"
cd "$TRASH/K" || exit 1
run git shared log p.h
expect_rc "clone: codice 1" 1
expect_err "clone: non collegato" "'p.h' is not linked here"

done_testing
