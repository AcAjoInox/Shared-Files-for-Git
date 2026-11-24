#!/bin/sh
# t0009-merge.sh - unione delle divergenze del branch (REQ-F-011,
# D-078; reportIdea B-13, B-17, B-18). Unioni tra versioni: t0024, t0025.

. "$(dirname "$0")/lib.sh"
. "$TEST_DIR/lib-merge.sh"

# --- divergenza del branch, unione pulita --------------------------------------
pair A B
cd "$TRASH/A" || exit 1
change A1 b c d e
git shared publish >/dev/null
T_A=$(tip)
cd "$TRASH/B" || exit 1
change a b c d E-b
expect_eq "pulita: divergente" "$(state)" "    state  diverged"
run git shared update
expect_rc "pulita: codice 0" 0
expect_out "pulita: unita, da committare" "f.h: merged. Commit it to confirm"
expect_eq "pulita: contenuto unito" "$(tr '\n' ' ' <f.h)" "A1 b c d E-b "
expect_eq "pulita: in staging" "$(git diff --cached --name-only)" "f.h"
expect_eq "pulita: stato da committare" "$(state)" "    state  applied, to commit"
expect_eq "pulita: la versione nuova discende da T" "$(tip ^)" "$T_A"
expect_fail "pulita: un solo genitore" git rev-parse -q --verify "refs/shared/$(shared_id f.h)^2"
expect_fail "pulita: nessuna unione registrata" has_marker
git commit -q -m "merge f.h"
expect_eq "pulita: allineato dopo il commit" "$(state)" "    state  aligned"
run git shared publish
expect_rc "pulita: publish accettato" 0
expect_out "pulita: inviata" "1 of 1 peers notified"
cd "$TRASH/A" || exit 1
run git shared update -c
expect_out "pulita: A la applica" "new version applied and committed"
expect_eq "pulita: A ha il contenuto unito" "$(tr '\n' ' ' <f.h)" "A1 b c d E-b "

# --- update -c committa l'unione -----------------------------------------------
cd "$TRASH/A" || exit 1
change A1 b C2 d E-b
git shared publish >/dev/null
cd "$TRASH/B" || exit 1
change A1 b C-b d E-b
run git shared update
expect_rc "conflitto: codice 1" 1
expect_err "conflitto: segnalato" "f.h: merge conflict"
git restore -s HEAD -SW -- f.h
change A1 b c d E-b2
run git shared update -c
expect_rc "update -c: codice 0" 0
expect_err "update -c: unione precedente abbandonata" "abandoned"
expect_out "update -c: unita e committata" "f.h: merged and committed"
expect_eq "update -c: allineato" "$(state)" "    state  aligned"
expect_eq "update -c: contenuto" "$(git show HEAD:f.h | tr '\n' ' ')" "A1 b C2 d E-b2 "
git shared publish >/dev/null
git -C ../A shared update -c >/dev/null 2>&1

# --- conflitto: stage nell'index, risoluzione, conclusione ---------------------
pair C D
cd "$TRASH/C" || exit 1
change a b C-c d e
git shared publish >/dev/null
T_C=$(tip)
cd "$TRASH/D" || exit 1
change a b C-d d e
run git shared update
expect_rc "conflitto: codice 1" 1
expect_err "conflitto: come uscirne" "git shared update --abort 'f.h'"
expect_eq "conflitto: come in git" "$(git status --short f.h)" "UU f.h"
expect_ok "conflitto: marker nostri" grep -q '^<<<<<<< ours' f.h
expect_ok "conflitto: marker loro" grep -q '^>>>>>>> theirs' f.h
expect_eq "conflitto: stato" "$(state)" "    state  merge in progress"
expect_ok "conflitto: unione registrata" has_marker
expect_eq "conflitto: T non cambia" "$(tip)" "$T_C"
run git shared publish
expect_rc "conflitto: publish rifiuta" 1
expect_err "conflitto: publish dice cosa fare" "merge in progress"
run git shared update
expect_rc "conflitto non risolto: update rifiuta" 1
expect_err "conflitto non risolto: messaggio" "merge conflict"

lines f.h a b C-cd d e
git add f.h
expect_eq "risolto: ancora in corso" "$(state)" "    state  merge in progress"
run git shared update
expect_rc "risolto: codice 0" 0
expect_out "risolto: unita" "f.h: merged. Commit it to confirm"
expect_fail "risolto: unione tolta" has_marker
expect_eq "risolto: versione figlia di T" "$(tip ^)" "$T_C"
expect_eq "risolto: da committare" "$(state)" "    state  applied, to commit"
git commit -q -m "risolto"
expect_eq "risolto: allineato" "$(state)" "    state  aligned"
run git shared publish
expect_out "risolto: inviata" "1 of 1 peers notified"

# --- abbandono con git restore (B-13) -------------------------------------------
cd "$TRASH/C" || exit 1
git shared update -c >/dev/null 2>&1
change a b C-c2 d e
git shared publish >/dev/null
cd "$TRASH/D" || exit 1
change a b C-d2 d e
git shared update >/dev/null 2>&1
expect_ok "abbandono: unione registrata" has_marker
git restore -s HEAD -SW -- f.h
expect_eq "abbandono: di nuovo divergente" "$(state)" "    state  diverged"
expect_eq "abbandono: file dell'utente" "$(tr '\n' ' ' <f.h)" "a b C-d2 d e "

# --- T avanza durante la risoluzione (B-17) --------------------------------------
git shared update >/dev/null 2>&1
cd "$TRASH/C" || exit 1
change A-c b C-c2 d e
git shared publish >/dev/null 2>&1
T_C2=$(tip)
cd "$TRASH/D" || exit 1
git shared status >/dev/null 2>&1 # D riceve (D-099)
expect_eq "B-17: T avanzata, ricevuta da status" "$(tip)" "$T_C2"
lines f.h a b C-cd2 d e
git add f.h
run git shared update -c
expect_rc "B-17: codice 0" 0
expect_eq "B-17: unita anche la versione arrivata dopo" \
	"$(git show HEAD:f.h | tr '\n' ' ')" "A-c b C-cd2 d e "
expect_eq "B-17: figlia della T nuova" "$(tip ^)" "$T_C2"
expect_eq "B-17: allineato" "$(state)" "    state  aligned"
git shared publish >/dev/null
git -C ../C shared update -c >/dev/null 2>&1

done_testing
