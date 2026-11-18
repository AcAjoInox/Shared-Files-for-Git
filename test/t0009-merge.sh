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
expect_rc "peer: publish accettato, E notificato" 0
T_F=$(tip)
run git shared update --all # l'avviso di E e' andato perso
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
expect_out "B-46: F pubblica la rimozione" "1 of 1 peers notified"
T_F2=$(tip)
cd "$TRASH/E" || exit 1
git reset -q --hard HEAD~1 # E torna al commit della sua versione T_E
git shared status >/dev/null 2>&1 # E riceve (D-099)
expect_eq "B-46: H di E e' la versione T_E" "$(git rev-parse HEAD:f.h)" \
	"$(git rev-parse "$T_E:content")"
expect_eq "B-46: T di E e' la versione di F" "$(tip)" "$T_F2"
expect_eq "B-46: da applicare, non divergente" "$(state)" \
	"    state  new version to apply"
run git shared publish
expect_no_out "B-46: publish non invia nulla" "peers notified"
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

# --- update: T avanza solo su una versione che la contiene (B-47) ---------------
# L ha i peer [M, N], in quest'ordine. N ha vN, che discende da vL; M ha vM,
# nata dalla prima versione. merge-base --independent restituisce vM e vN
# nell'ordine dei peer: T deve avanzare su vN, l'unica che contiene vL, e
# vM si unisce dopo, con due genitori.
pair L M
make_repo N
cd "$TRASH/N" || exit 1
put o.txt o
git shared link -c ../L/f.h >/dev/null
cd "$TRASH" && mv M M.off
cd "$TRASH/L" || exit 1
change L1 b c d e
git shared publish >/dev/null 2>&1 # avviso solo a N
git -C "$TRASH/N" shared status >/dev/null 2>&1 # N riceve prima che L sparisca
T_L=$(tip)
cd "$TRASH" && mv L L.off
cd "$TRASH/N" || exit 1
git shared update -c >/dev/null 2>&1
change L1 b N3 d e
git shared publish >/dev/null 2>&1 # nessun peer raggiungibile
T_N=$(tip)
cd "$TRASH" && mv L.off L && mv M.off M
cd "$TRASH/M" || exit 1
change a b c d M5
git shared publish >/dev/null 2>&1 # notifica L e N
T_M=$(tip)
cd "$TRASH/L" || exit 1
expect_eq "B-47: peer di L nell'ordine M, N" \
	"$(git config --get-all "shared.$(shared_id f.h).peer" | sed 's|.*/||' | tr '\n' ' ')" \
	"M N "
run git shared update --all # vN senza avviso: L era spostata
expect_rc "B-47: codice 0" 0
expect_out "B-47: versione ricevuta" "new version received"
expect_ok "B-47: la T di prima e' antenata della nuova" \
	git merge-base --is-ancestor "$T_L" "$(tip)"
expect_eq "B-47: primo genitore la versione che contiene T" "$(tip ^1)" "$T_N"
expect_eq "B-47: secondo genitore l'altra versione" "$(tip ^2)" "$T_M"
expect_eq "B-47: contenuto unito" "$(tr '\n' ' ' <f.h)" "L1 b N3 d M5 "

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
run git shared update --all # l'avviso di O e' andato perso
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
expect_err "abort con -c: messaggio" "--abort cannot be used with -c, -m, -f or --all"
run git shared update -f --abort
expect_rc "abort con -f: uso" 129

# --- conclusione di un conflitto tra versioni: una sola versione (B-49) -----------
# La risoluzione diventa una versione con genitori (T di partenza, versione
# del peer); la versione del peer, gia' contenuta, non si unisce di nuovo.
pair Q R
cd "$TRASH" && mv R R.off
cd "$TRASH/Q" || exit 1
change Q1 b c d e
git shared publish >/dev/null 2>&1
T_Q=$(tip)
cd "$TRASH" && mv R.off R
cd "$TRASH/R" || exit 1
change R1 b c d e
git shared publish >/dev/null 2>&1 # notifica Q
T_R=$(tip)
run git shared update --all # l'avviso di Q e' andato perso
expect_rc "B-49: conflitto" 1
lines f.h QR1 b c d e
git add f.h
run git shared update -c
expect_rc "B-49: conclusa" 0
expect_out "B-49: unita e committata" "f.h: merged and committed"
expect_eq "B-49: primo genitore la T di partenza" "$(tip ^1)" "$T_R"
expect_eq "B-49: secondo genitore la versione del peer" "$(tip ^2)" "$T_Q"
expect_eq "B-49: contenuto della risoluzione" "$(git show HEAD:f.h | tr '\n' ' ')" \
	"QR1 b c d e "
expect_eq "B-49: allineato" "$(state)" "    state  aligned"
run git shared publish
expect_out "B-49: inviata" "1 of 1 peers notified"
cd "$TRASH/Q" || exit 1
run git shared update -c
expect_out "B-49: Q applica" "new version applied and committed"
expect_eq "B-49: Q allineato" "$(state)" "    state  aligned"

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
