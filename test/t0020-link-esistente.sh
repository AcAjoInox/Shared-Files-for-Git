#!/bin/sh
# t0020-link-esistente.sh - link su una destinazione che esiste gia'
# (REQ-F-002, D-062, D-029, D-102) e su una gia' shared con un altro ID
# (--adopt, D-064).

. "$(dirname "$0")/lib.sh"

V1='uno
due
tre'
V2="$V1
quattro"

# status_of: stato del primo file shared della repo corrente
status_of() {
	git shared status | sed -n 's/^    state  //p' | head -n 1
}

# repo_with <nome> <contenuto>: repo con c.h committato
repo_with() {
	make_repo "$1"
	cd "$TRASH/$1" || exit 1
	put x.c x
	put c.h "$2"
}

# --- A: c.h con due versioni (V1, V2) --------------------------------------------
make_repo A
cd "$TRASH/A" || exit 1
put c.h "$V1"
git shared set -c c.h >/dev/null
ID=$(shared_id c.h)
put c.h "$V2"
git shared publish >/dev/null

# --- contenuto uguale all'ultima versione: il file resta, allineato ---------------
repo_with D1 "$V2"
printf 'modifica locale\n' >>c.h
run git shared link ../A/c.h
expect_rc "uguale a T: codice 0" 0
expect_out "uguale a T: collegato" "'c.h' linked as 'c.h' from"
expect_out "uguale a T: il file resta" "c.h: the existing file was kept"
expect_eq "uguale a T: in staging solo .gitshared" "$(git diff --cached --name-only)" ".gitshared"
expect_ok "uguale a T: modifica locale intatta" grep -q "modifica locale" c.h
expect_eq "uguale a T: attesa di link" "$(pending_op "$ID")" "link"
git commit -q -m "link"
expect_eq "uguale a T: allineato dopo il commit" "$(status_of)" "aligned"
expect_ok "uguale a T: modifica locale ancora non committata" grep -q "modifica locale" c.h
expect_ok "uguale a T: A conosce D1" \
	sh -c "git -C '$TRASH/A' config --get-all shared.$ID.peer | grep -q D1"

# --- contenuto uguale a una versione vecchia: da applicare ------------------------
repo_with D2 "$V1"
run git shared link -c ../A/c.h
expect_rc "versione vecchia: codice 0" 0
expect_out "versione vecchia: confermato" "'c.h' linked as 'c.h' from"
expect_eq "versione vecchia: il commit contiene solo .gitshared" \
	"$(git show --name-only --format= HEAD)" ".gitshared"
expect_eq "versione vecchia: da applicare" "$(status_of)" "new version to apply"
run git shared update -c
expect_eq "versione vecchia: update porta all'ultima" "$(cat c.h)" "$V2"

# --- nessuna versione in comune: errore, --merge, -f --------------------------------
repo_with D4 "altro"
run git shared link ../A/c.h
expect_rc "senza base: codice 1" 1
expect_err "senza base: errore" "'c.h' already exists and has nothing in common with the shared file"
expect_eq "senza base: nulla in staging" "$(git diff --cached --name-only)" ""
expect_eq "senza base: nessuna attesa" "$(git config "shared.$ID.pending")" ""
expect_fail "senza base: nessun .gitshared" test -e .gitshared
run git shared link --adopt ../A/c.h
expect_rc "--adopt su un file non shared: codice 1" 1
expect_err "--adopt su un file non shared: errore" "--adopt is not needed"
run git shared link -f --merge ../A/c.h
expect_rc "-f con --merge: codice 129" 129
expect_err "-f con --merge: errore" "cannot be used together"
run git shared link --merge ../A/c.h
expect_rc "--merge: codice 0" 0
expect_out "--merge: dice cosa fare" "c.h: nothing in common with the shared file: once committed, 'git shared update' merges them"
expect_eq "--merge: file non toccato" "$(cat c.h)" "altro"
git commit -q -m "link --merge"
expect_eq "--merge: divergente dopo il commit" "$(status_of)" "diverged"
run git shared update
expect_rc "--merge: update trova il conflitto" 1
expect_err "--merge: conflitto" "c.h: merge conflict"
git shared update --abort >/dev/null 2>&1
expect_eq "--merge: abbandono, file di prima" "$(cat c.h)" "altro"

repo_with D5 "altro"
run git shared link -f -c ../A/c.h
expect_rc "-f: codice 0" 0
expect_eq "-f: sovrascritto con l'ultima versione" "$(cat c.h)" "$V2"
expect_eq "-f: il commit contiene .gitshared e il file" \
	"$(git show --name-only --format= HEAD | tr '\n' ' ')" ".gitshared c.h "
expect_eq "-f: allineato" "$(status_of)" "aligned"

# --- file non tracciato, cartella, file in staging -----------------------------------
make_repo D6
cd "$TRASH/D6" || exit 1
put x.c x
printf 'mio\n' >c.h
run git shared link ../A/c.h
expect_rc "non tracciato: codice 1" 1
expect_err "non tracciato: errore" "'c.h' is not tracked by git"
run git shared link -f ../A/c.h
expect_rc "non tracciato con -f: codice 0" 0
expect_eq "non tracciato con -f: sovrascritto" "$(cat c.h)" "$V2"
expect_eq "non tracciato con -f: in staging" \
	"$(git diff --cached --name-only | tr '\n' ' ')" ".gitshared c.h "

make_repo D7
cd "$TRASH/D7" || exit 1
put x.c x
mkdir c.h
run git shared link -f ../A/c.h
expect_rc "cartella: codice 1" 1
expect_err "cartella: errore" "is a directory"

make_repo D8
cd "$TRASH/D8" || exit 1
put x.c x
printf '%s\n' "$V2" >c.h
git add c.h
run git shared link -c ../A/c.h
expect_rc "in staging, non committato: codice 0" 0
expect_eq "in staging: il commit contiene anche il file" \
	"$(git show --name-only --format= HEAD | tr '\n' ' ')" ".gitshared c.h "
expect_eq "in staging: allineato" "$(status_of)" "aligned"

# --- annullamento: il file resta com'era ----------------------------------------------
repo_with D9 "$V1"
git shared link ../A/c.h >/dev/null
git rm -q --cached .gitshared
rm .gitshared
run git shared status
expect_err "annullato: attesa scartata" "discarded pending operation on 'c.h'"
expect_eq "annullato: file intatto" "$(cat c.h)" "$V1"
expect_eq "annullato: nessuna attesa" "$(git config "shared.$ID.pending")" ""

# --- destinazione gia' shared con un altro ID: --adopt (D-064) --------------------------
repo_with E "$V1"
git shared set -c c.h >/dev/null
OLD=$(shared_id c.h)
make_repo F
cd "$TRASH/F" || exit 1
put f.c f
git shared link -c ../E/c.h >/dev/null 2>&1
cd "$TRASH/E" || exit 1
expect_ok "F conosce E con il vecchio ID" git -C ../F config "shared.$OLD.peer"
run git shared link ../A/c.h
expect_rc "altro ID senza --adopt: codice 1" 1
expect_err "altro ID senza --adopt: errore" "'c.h' is already shared here as 'c.h', with another ID: use --adopt"
run git shared link --adopt ../A/c.h
expect_rc "--adopt: codice 0" 0
expect_out "--adopt: dice cosa succede" "c.h: takes the ID of the source; the old ID $OLD is unlinked by the same commit"
expect_out "--adopt: il file resta" "c.h: the existing file was kept"
expect_eq "--adopt: .gitshared con l'ID della sorgente" "$(shared_id c.h)" "$ID"
expect_eq "--adopt: attesa di link" "$(pending_op "$ID")" "link"
expect_eq "--adopt: attesa di unlink del vecchio ID" "$(pending_op "$OLD")" "unlink"
expect_eq "--adopt: F non sa ancora nulla" "$(git -C ../F config --get-all "shared.$OLD.peer" | wc -l | tr -d ' ')" "1"
git commit -q -m "adotto l'ID di A"
expect_eq "--adopt: dati del vecchio ID tolti" "$(git config --get-regexp "shared\\.$OLD\\." | wc -l | tr -d ' ')" "0"
expect_fail "--adopt: vecchia catena tolta" git rev-parse -q --verify "refs/shared/$OLD"
expect_fail "--adopt: F ha dimenticato E" git -C ../F config "shared.$OLD.peer"
expect_ok "--adopt: A conosce E" \
	sh -c "git -C '$TRASH/A' config --get-all shared.$ID.peer | grep -q '/E$'"
expect_eq "--adopt: da applicare" "$(status_of)" "new version to apply"

# -c e contenuto uguale a T: allineato subito.
repo_with G "$V2"
git shared set -c c.h >/dev/null
OLD_G=$(shared_id c.h)
run git shared link --adopt -c ../A/c.h
expect_rc "--adopt -c: codice 0" 0
expect_eq "--adopt -c: allineato" "$(status_of)" "aligned"
expect_eq "--adopt -c: vecchio ID tolto" "$(git config "shared.$OLD_G.pending")$(git config --get-regexp "shared\\.$OLD_G\\." | wc -l | tr -d ' ')" "0"

# Vecchia identita' con un set in attesa: rifiutato.
repo_with H "$V2"
git shared set c.h >/dev/null
run git shared link --adopt ../A/c.h
expect_rc "--adopt con un set in attesa: codice 1" 1
expect_err "--adopt con un set in attesa: errore" "has a pending operation"

# Annullamento di --adopt: torna la vecchia identita'.
repo_with J "$V1"
git shared set -c c.h >/dev/null
OLD_J=$(shared_id c.h)
git shared link --adopt ../A/c.h >/dev/null
git restore --staged --worktree .gitshared
run git shared status
expect_eq "--adopt annullato: vecchio ID in .gitshared" "$(shared_id c.h)" "$OLD_J"
expect_eq "--adopt annullato: nessuna attesa" \
	"$(git config "shared.$OLD_J.pending")$(git config "shared.$ID.pending")" ""
expect_ok "--adopt annullato: vecchia catena intatta" git rev-parse -q --verify "refs/shared/$OLD_J"
expect_eq "--adopt annullato: allineato" "$(status_of)" "aligned"

# --- ri-aggancio: -f, --merge e --adopt non c'entrano ------------------------------------
git clone -q "$TRASH/D1" "$TRASH/K"
cd "$TRASH/K" || exit 1
run git shared link -f ../A/c.h
expect_rc "ri-aggancio con -f: codice 1" 1
expect_err "ri-aggancio con -f: errore" "-f, --merge and --adopt are not used"

# --- modifica locale su una versione: divergente con base vera ---------------------
# Per ultimo: update crea una versione di unione nella catena di D3, che i
# link successivi riceverebbero (D-100).
repo_with D3 "$V1"
put c.h "zero
$V1"
run git shared link -c ../A/c.h
expect_rc "modifica su una versione: codice 0" 0
expect_eq "modifica su una versione: divergente" "$(status_of)" "diverged"
run git shared update -c
expect_rc "modifica su una versione: update unisce senza conflitti" 0
expect_eq "modifica su una versione: entrambe le modifiche" \
	"$(tr '\n' ' ' <c.h)" "zero uno due tre quattro "

done_testing
