#!/bin/sh
# t0025-merge-abbandono.sh - unione abbandonata, update --abort e casi limite
# (REQ-F-011, D-029, D-092, D-093; reportIdea B-34, B-41, B-42, B-48).

. "$(dirname "$0")/lib.sh"
. "$TEST_DIR/lib-merge.sh"

# --- unione abbandonata: il primo comando la toglie (B-48) ------------------------
# Conflitto tra versioni, abbandono con git restore come dice il manuale.
# Il primo comando git shared deve togliere l'unione scaduta: una modifica
# messa in staging dopo non e' una risoluzione, e update non deve creare una
# versione che dichiara di contenere quella del peer senza le sue righe.
pair O P
cd "$TRASH" && mv P P.off
cd "$TRASH/O" || exit 1
change O1 b c d e
git shared publish >/dev/null 2>&1
T_O=$(tip)
cd "$TRASH" && mv P.off P
cd "$TRASH/P" || exit 1
change P1 b c d e
git shared publish >/dev/null 2>&1 # notifica O
T_P=$(tip)
run git shared update --scan # l'avviso di O e' andato perso
expect_rc "B-48: conflitto" 1
expect_ok "B-48: unione registrata" has_marker
git restore -s HEAD -SW -- f.h
expect_eq "B-48: dopo l'abbandono allineato" "$(state)" "    state  aligned"
expect_fail "B-48: status toglie l'unione scaduta" has_marker
lines f.h P1 b c d P5
git add f.h
expect_eq "B-48: la modifica in staging non e' una risoluzione" "$(state)" \
	"    state  aligned"
run git shared update -c
expect_rc "B-48: update rifiuta" 1
expect_err "B-48: modifica locale protetta" "has local changes"
expect_eq "B-48: nessuna versione nuova" "$(tip)" "$T_P"
expect_eq "B-48: modifica intatta" "$(git show :f.h | tr '\n' ' ')" "P1 b c d P5 "
git restore -s HEAD -SW -- f.h

# Anche publish (e ogni comando che ricava lo stato) toglie l'unione scaduta.
git shared update >/dev/null 2>&1
expect_ok "B-48: di nuovo in conflitto" has_marker
git restore -s HEAD -SW -- f.h
git shared publish >/dev/null 2>&1
expect_fail "B-48: publish toglie l'unione scaduta" has_marker

# --- update --abort: rinuncia in un solo comando (B-48, D-093) --------------------
# La procedura documentata: nessun momento in cui una modifica in staging
# possa passare per la risoluzione.
git shared update >/dev/null 2>&1
expect_eq "abort: conflitto aperto" "$(git status --short f.h)" "UU f.h"
run git shared update --abort
expect_rc "abort: codice 0" 0
expect_out "abort: messaggio" "f.h: merge abandoned"
expect_fail "abort: unione tolta" has_marker
expect_eq "abort: nessun conflitto nell'index" "$(git status --short f.h)" ""
expect_eq "abort: file di HEAD" "$(tr '\n' ' ' <f.h)" "P1 b c d e "
expect_eq "abort: T non cambia" "$(tip)" "$T_P"
expect_eq "abort: allineato" "$(state)" "    state  aligned"
lines f.h P1 b c d P5
git add f.h
expect_eq "abort: la modifica dopo non e' una risoluzione" "$(state)" \
	"    state  aligned"
run git shared update -c
expect_rc "abort: update protegge la modifica" 1
expect_eq "abort: nessuna versione nuova" "$(tip)" "$T_P"
git restore -s HEAD -SW -- f.h

# Con la risoluzione gia' in staging, e per un file indicato.
git shared update >/dev/null 2>&1
lines f.h OP1 b c d e
git add f.h
expect_eq "abort: risoluzione in staging" "$(state)" "    state  merge in progress"
run git shared update --abort f.h
expect_rc "abort <file>: codice 0" 0
expect_eq "abort <file>: file di HEAD" "$(git show :f.h | tr '\n' ' ')" "P1 b c d e "
expect_fail "abort <file>: unione tolta" has_marker

# Nessuna unione in corso; opzioni incompatibili.
run git shared update --abort
expect_rc "abort senza unione: codice 1" 1
expect_err "abort senza unione: messaggio" "no merge in progress"
run git shared update --abort f.h
expect_rc "abort <file> senza unione: codice 1" 1
expect_err "abort <file> senza unione: messaggio" "f.h: no merge in progress"
run git shared update --abort -c
expect_rc "abort con -c: uso" 129
expect_err "abort con -c: messaggio" "--abort cannot be used with -c, -m, -f, --scan or --peers"
run git shared update -f --abort
expect_rc "abort con -f: uso" 129

# --- squash: falso divergente, si chiude da solo (B-42) --------------------------
pair G H
cd "$TRASH/G" || exit 1
change G1 b c d e
git shared publish >/dev/null
cd "$TRASH/H" || exit 1
git shared update -c >/dev/null
change G1 b c d H5
git reset -q --soft HEAD~2
git commit -q -m squash
expect_eq "squash: divergente" "$(state)" "    state  diverged"
run git shared update
expect_rc "squash: codice 0" 0
expect_out "squash: niente da committare" "f.h: merged, nothing to commit"
expect_eq "squash: allineato" "$(state)" "    state  aligned"
expect_eq "squash: file intatto" "$(tr '\n' ' ' <f.h)" "G1 b c d H5 "
run git shared publish
expect_out "squash: inviata" "1 of 1 peers notified"

# --- amend dopo publish (B-34) ----------------------------------------------------
cd "$TRASH/G" || exit 1
git shared update -c >/dev/null
change G1 b c d H5 six
git shared publish >/dev/null
lines f.h G0 b c d H5 six
git commit -q -a --amend -m "amend" 2>/dev/null
expect_eq "amend: divergente" "$(state)" "    state  diverged"
run git shared update
expect_rc "amend: codice 0" 0
expect_out "amend: niente da committare" "f.h: merged, nothing to commit"
expect_eq "amend: allineato" "$(state)" "    state  aligned"
run git shared publish
expect_rc "amend: publish accettato" 0

# --- link e modifica prima del commit: nessuna base (B-41, D-029) -----------------
make_repo I
cd "$TRASH/I" || exit 1
put o.txt o
git shared link ../G/f.h >/dev/null
printf 'in piu\n' >>f.h
git add f.h
git commit -q -m "link e modifica"
expect_eq "senza base: divergente" "$(state)" "    state  diverged"
run git shared update
expect_rc "senza base: conflitto" 1
expect_eq "senza base: come in git" "$(git status --short f.h)" "UU f.h"
git checkout -q --ours -- f.h
printf 'risolto\n' >>f.h
git add f.h
run git shared update -c
expect_rc "senza base: conclusa" 0
expect_eq "senza base: allineato" "$(state)" "    state  aligned"

# --- modifiche locali protette anche nell'unione -----------------------------------
pair J K
cd "$TRASH/J" || exit 1
change J1 b c d e
git shared publish >/dev/null
cd "$TRASH/K" || exit 1
change a b c d K5
printf 'non committata\n' >>f.h
run git shared update
expect_rc "modifiche locali: codice 1" 1
expect_err "modifiche locali: rifiuto" "has local changes"
expect_ok "modifiche locali: file non toccato" grep -q "non committata" f.h

done_testing
