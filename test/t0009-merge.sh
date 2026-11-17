#!/bin/sh
# t0009-merge.sh - unione delle divergenze (REQ-F-011, D-078; reportIdea
# B-13, B-17, B-18, B-34, B-41, B-42).

. "$(dirname "$0")/lib.sh"

# lines <file> <r1> ... : una riga per argomento
lines() {
	f=$1
	shift
	printf '%s\n' "$@" >"$f"
}

# pair <A> <B>: A condivide f.h (5 righe), B lo collega
pair() {
	make_repo "$1"
	cd "$TRASH/$1" || exit 1
	lines f.h a b c d e
	git add f.h
	git commit -q -m init
	git shared set -c f.h >/dev/null
	make_repo "$2"
	cd "$TRASH/$2" || exit 1
	put o.txt o
	git shared link -c "../$1/f.h" >/dev/null
}

# change <r1> ... : riscrive f.h e committa
change() {
	lines f.h "$@"
	git commit -q -a -m "f.h: $*"
}

state() {
	git shared status 2>/dev/null | grep state
}

tip() {
	git rev-parse "refs/shared/$(shared_id f.h)$1"
}

has_marker() {
	git rev-parse -q --verify "refs/shared-merge/$(shared_id f.h)" >/dev/null
}

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
expect_out "pulita: inviata" "sent to 1 of 1 peers"
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
expect_err "conflitto: come uscirne" "git restore -s HEAD -SW -- 'f.h'"
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
expect_out "risolto: inviata" "sent to 1 of 1 peers"

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
expect_eq "B-17: T avanzata dal push" "$(tip)" "$T_C2"
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

# --- due peer pubblicano senza vedersi: versione con due genitori ----------------
pair E F
cd "$TRASH" || exit 1
mv F F.off
cd "$TRASH/E" || exit 1
change E1 b c d e
git shared publish >/dev/null 2>&1
T_E=$(tip)
cd "$TRASH" || exit 1
mv F.off F
cd "$TRASH/F" || exit 1
change a b c d F5
run git shared publish
expect_rc "peer: push rifiutato" 1
T_F=$(tip)
run git shared update
expect_rc "peer: codice 0" 0
expect_out "peer: unite" "f.h: merged. Commit it to confirm"
expect_eq "peer: contenuto" "$(tr '\n' ' ' <f.h)" "E1 b c d F5 "
expect_eq "peer: primo genitore la T locale" "$(tip ^1)" "$T_F"
expect_eq "peer: secondo genitore la versione del peer" "$(tip ^2)" "$T_E"
git commit -q -m "merge f.h"
run git shared publish
expect_rc "peer: publish accettato" 0
cd "$TRASH/E" || exit 1
run git shared update -c
expect_rc "peer: E applica" 0
expect_eq "peer: E ha il contenuto unito" "$(tr '\n' ' ' <f.h)" "E1 b c d F5 "
expect_eq "peer: E allineato" "$(state)" "    state  aligned"

# --- il peer la cui versione e' stata unita riceve l'unione (B-46) ---------------
# F ha unito la versione T_E di E: la versione M ha T_E come secondo
# genitore. F toglie la riga di E e pubblica V2. E, che ha nel branch il
# contenuto di T_E, deve risultare da applicare (base T_E), non divergente
# con base la prima versione, e il suo update non deve rimettere la riga.
cd "$TRASH/F" || exit 1
expect_eq "B-46: F allineato dopo l'unione" "$(state)" "    state  aligned"
change a b c d F5
run git shared publish
expect_out "B-46: F pubblica la rimozione" "sent to 1 of 1 peers"
T_F2=$(tip)
cd "$TRASH/E" || exit 1
git reset -q --hard HEAD~1 # E torna al commit della sua versione T_E
expect_eq "B-46: H di E e' la versione T_E" "$(git rev-parse HEAD:f.h)" \
	"$(git rev-parse "$T_E:content")"
expect_eq "B-46: T di E e' la versione di F" "$(tip)" "$T_F2"
expect_eq "B-46: da applicare, non divergente" "$(state)" \
	"    state  new version to apply"
run git shared publish
expect_no_out "B-46: publish non invia nulla" "sent to"
run git shared update -c
expect_rc "B-46: update codice 0" 0
expect_out "B-46: applicata, non unita" "new version applied and committed"
expect_eq "B-46: la riga tolta da F resta tolta" \
	"$(git show HEAD:f.h | tr '\n' ' ')" "a b c d F5 "
expect_eq "B-46: T non cambia" "$(tip)" "$T_F2"
expect_eq "B-46: allineato" "$(state)" "    state  aligned"

# La versione T_E in staging, sopra un HEAD piu' vecchio, non e' lavoro
# dell'utente: e' una versione della catena (D-076), update la sostituisce.
git reset -q --hard HEAD~2
git show "$T_E:content" >f.h
git add f.h
run git shared update -c
expect_rc "B-46: versione unita in staging non e' modifica locale" 0
expect_eq "B-46: applicata l'ultima versione" \
	"$(git show HEAD:f.h | tr '\n' ' ')" "a b c d F5 "

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
expect_out "squash: inviata" "sent to 1 of 1 peers"

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
