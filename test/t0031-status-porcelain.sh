#!/bin/sh
# t0031-status-porcelain.sh - git shared status --porcelain (REQ-F-012,
# D-119): il formato per i programmi. Blocca le righe e le parole chiave:
# un cambio qui e' un cambio del patto, quindi "# porcelain 2".
#
# Le note che chiedono scenari lunghi (stranger, to-merge, announced,
# chain-missing, renamed, elsewhere, branches-gone, creator, authority,
# heir, succession, creator-back, auth-change) sono provate accanto alla
# frase corrispondente, nei test che preparano quegli scenari.

. "$(dirname "$0")/lib.sh"

top() {
	git -C "$TRASH/$1" rev-parse --show-toplevel
}
blob() { # <repo> <rev:path>
	git -C "$TRASH/$1" rev-parse "$2"
}
tip() {
	git -C "$TRASH/$1" rev-parse -q --verify "refs/shared/$ID"
}
# porc_lines <tipo>: le righe di $OUT di quel tipo
porc_lines() {
	printf '%s\n' "$OUT" | grep "^$1 "
}

# --- argomenti --------------------------------------------------------------------------
make_repo "A dir"
cd "$TRASH/A dir" || exit 1
put o.txt o
run git shared status --porcelain=2
expect_rc "formato sconosciuto: codice 129" 129
run git shared status -h
expect_out "uso: --porcelain" "--porcelain  stable output for scripts, one line per fact"
expect_out "uso: -z" "-z           like --porcelain, each line ending with NUL"
run git shared status --help
expect_out "manuale: --porcelain" "--porcelain"

# --- nessun file shared: solo l'intestazione -----------------------------------------------
TOP_A=$(top "A dir")
run git shared status --porcelain
expect_rc "nessun file: codice 0" 0
expect_eq "nessun file: solo l'intestazione" "$OUT" "# porcelain 1
# repo $TOP_A
# branch main"

# --- allineato: l'output intero, path e nome con spazi --------------------------------------
put "p q.h" v1
git shared set -c "p q.h" >/dev/null
ID=$(shared_id "p q.h")
make_repo B
cd "$TRASH/B" || exit 1
put o.txt o
git shared link -c "../A dir/p q.h" >/dev/null
TOP_B=$(top B)
cd "$TRASH/A dir" || exit 1
T0=$(tip "A dir")
H1=$(blob "A dir" "HEAD:p q.h")
run git shared status --porcelain
expect_rc "allineato: codice 0" 0
expect_eq "allineato: output intero" "$OUT" "# porcelain 1
# repo $TOP_A
# branch main
file $ID aligned active p q.h
name $ID p q.h
branch $ID main
head $ID $H1
tip $ID $T0 $H1"
FULL=$OUT
git shared status --porcelain >"$TRASH/p.out"
expect_eq "solo LF, anche su Windows" "$(tr -cd '\r' <"$TRASH/p.out" | wc -c | tr -d ' ')" "0"
run git shared status --porcelain=1
expect_eq "--porcelain=1: lo stesso" "$OUT" "$FULL"

# -z: ogni riga finisce con NUL, nessun a capo
git shared status -z >"$TRASH/z.out"
expect_eq "-z: nessun a capo" "$(tr -cd '\n' <"$TRASH/z.out" | wc -c | tr -d ' ')" "0"
expect_eq "-z: NUL come fine riga" "$(tr '\0' '\n' <"$TRASH/z.out")" "$FULL"
expect_eq "-z: anche l'ultima riga" "$(tail -c 1 "$TRASH/z.out" | od -An -c | tr -d ' ')" '\0'

# niente traduzione, niente consigli
git config shared.lang it
run git shared status --porcelain
expect_eq "shared.lang it: lo stesso" "$OUT" "$FULL"
git config --unset shared.lang
expect_no_out "nessun consiglio" "git shared"

# --peers: un peer con la stessa versione
run git shared status --porcelain --peers
expect_eq "--peers: riga peer" "$(porc_lines peer)" "peer $ID same $TOP_B"

# --- da pubblicare, poi annunciato al peer --------------------------------------------------
put "p q.h" v2
H2=$(blob "A dir" "HEAD:p q.h")
run git shared status --porcelain
expect_out "da pubblicare: stato" "file $ID to-publish active p q.h"
expect_out "da pubblicare: H" "head $ID $H2"
expect_out "da pubblicare: T resta" "tip $ID $T0 $H1"
expect_no_out "da pubblicare: nessuna base" "base "
git shared publish >/dev/null 2>&1
T1=$(tip "A dir")
run git shared status --porcelain --peers
expect_out "pubblicato: allineato" "file $ID aligned active p q.h"
expect_out "pubblicato: peer annunciato" "peer $ID announced $TOP_B"

# --- da applicare, da committare ------------------------------------------------------------
cd "$TRASH/B" || exit 1
run git shared status --porcelain
expect_out "B: da applicare" "file $ID to-apply active p q.h"
expect_out "B: H vecchio" "head $ID $H1"
expect_out "B: T nuovo" "tip $ID $T1 $H2"
git shared update >/dev/null 2>&1
run git shared status --porcelain
expect_out "B: da committare" "file $ID to-commit active p q.h"
git commit -q -m update
run git shared status --porcelain
expect_out "B: allineato" "file $ID aligned active p q.h"

# --- divergente, con la base ---------------------------------------------------------------
cd "$TRASH/A dir" || exit 1
put "p q.h" "v2
a3"
git shared publish >/dev/null 2>&1
cd "$TRASH/B" || exit 1
put "p q.h" "b3
v2"
run git shared status --porcelain
expect_out "B: divergente" "file $ID diverged active p q.h"
expect_out "B: base = v2" "base $ID $H2"
cd "$TRASH/A dir" || exit 1

# --- sospeso su un altro branch, HEAD staccata ----------------------------------------------
git checkout -q -b feat
run git shared status --porcelain
expect_out "altro branch: intestazione" "# branch feat"
expect_out "altro branch: sospeso, stato vero" "file $ID aligned suspended p q.h"
expect_out "altro branch: branch attivo" "branch $ID main"
git checkout -q --detach
run git shared status --porcelain
expect_out "HEAD staccata: intestazione" "# detached"
expect_no_out "HEAD staccata: nessun # branch" "# branch"
git checkout -q main

# --- scollegato (un clone), incoerente, voce non valida -------------------------------------
git clone -q "$TRASH/A dir" "$TRASH/K"
cd "$TRASH/K" || exit 1
run git shared status --porcelain
expect_out "clone: scollegato" "file $ID unlinked active p q.h"
expect_no_out "clone: nessuna T" "tip "
cd "$TRASH/A dir" || exit 1
git rm -q "p q.h"
git commit -q -m rm 2>/dev/null
run git shared status --porcelain
expect_out "file cancellato: incoerente" "file $ID inconsistent active p q.h"
expect_no_out "file cancellato: nessuna H" "head "
expect_out "file cancellato: T c'e' lo stesso" "tip $ID $(tip "A dir") "
git config -f .gitshared shared.rotto.path ""
run git shared status --porcelain
expect_out "voce non valida: riga invalid" "invalid - rotto"
expect_err "voce non valida: avviso su stderr" "rotto"

# --- in attesa di commit --------------------------------------------------------------------
make_repo P
cd "$TRASH/P" || exit 1
put c.h c
git shared set c.h >/dev/null
IDP=$(shared_id c.h)
run git shared status --porcelain
expect_out "set non committato: in attesa" "file $IDP pending active c.h"

done_testing
