#!/bin/sh
# t0007-tocommit.sh - stato "applicato, da committare" e modifiche locali
# (REQ-F-009, REQ-F-010, REQ-F-012, D-076; reportIdea B-26, B-30).

. "$(dirname "$0")/lib.sh"

# publish_from_a <contenuto>: A committa e pubblica una nuova versione
publish_from_a() {
	(cd "$TRASH/A" && put c.h "$1" && git shared publish >/dev/null 2>&1)
}

make_repo A
cd "$TRASH/A" || exit 1
put c.h v1
git shared set -c c.h >/dev/null
make_repo B
cd "$TRASH/B" || exit 1
put x.txt x
git shared link -c ../A/c.h >/dev/null

# --- update senza commit: "da committare" -----------------------------------
publish_from_a v2
cd "$TRASH/B" || exit 1
run git shared update
expect_out "update: applicata" "c.h: new version applied. Commit it to confirm."
run git shared status
expect_out "dopo update: da committare" "applied, to commit"
expect_no_out "dopo update: non piu' da applicare" "new version to apply"

run git shared update
expect_rc "update ripetuto: codice 0" 0
expect_out "update ripetuto: gia' applicata" "applied but not committed"
run git shared publish
expect_rc "publish su da committare: codice 1" 1
expect_err "publish su da committare: errore" "applied but not committed"

# --- arriva una versione nuova prima del commit (B-26) ----------------------
publish_from_a v3
cd "$TRASH/B" || exit 1
run git shared update
expect_rc "seconda versione: codice 0 senza -f" 0
expect_out "seconda versione: applicata" "c.h: new version applied"
expect_eq "seconda versione: file aggiornato" "$(cat c.h)" "v3"
expect_eq "seconda versione: index aggiornato" "$(git show :c.h)" "v3"
run git shared status
expect_out "seconda versione: da committare" "applied, to commit"

# --- modifica dell'utente dopo l'update: protetta ---------------------------
printf 'v3 + modifica\n' >c.h
publish_from_a v4
cd "$TRASH/B" || exit 1
run git shared update
expect_rc "modifica nel working tree: codice 1" 1
expect_err "modifica nel working tree: rifiutata" "has local changes"
expect_eq "modifica nel working tree: conservata" "$(cat c.h)" "v3 + modifica"
run git shared update -c
expect_rc "update -c con modifica: codice 1" 1
expect_eq "update -c con modifica: niente commit" "$(git show HEAD:c.h)" "v1"
git checkout -q -- c.h

# --- update -c da "da committare": commit dei soli file applicati -----------
printf 'altro\n' >x.txt
git add x.txt
run git shared update -c
expect_rc "update -c: codice 0" 0
expect_out "update -c: committata" "c.h: new version applied and committed."
expect_eq "update -c: HEAD ha v4" "$(git show HEAD:c.h)" "v4"
expect_eq "update -c: commit con il solo c.h" "$(git show --name-only --format= HEAD)" "c.h"
expect_eq "update -c: x.txt resta in staging" "$(git diff --cached --name-only)" "x.txt"
git reset -q x.txt
git checkout -q -- x.txt
run git shared status
expect_out "dopo update -c: allineato" "aligned"

# --- annullare l'update: torna "da applicare" -------------------------------
publish_from_a v5
cd "$TRASH/B" || exit 1
git shared update >/dev/null
git restore --staged --worktree c.h
run git shared status
expect_out "update annullato: da applicare" "new version to apply"

# --- modifica in staging, file su disco uguale a HEAD (B-30) ----------------
printf 'lavoro in staging\n' >c.h
git add c.h
git show HEAD:c.h >c.h
run git shared update
expect_rc "modifica in staging: codice 1" 1
expect_err "modifica in staging: rifiutata" "has local changes"
expect_eq "modifica in staging: conservata" "$(git show :c.h)" "lavoro in staging"

# Modifica in staging uguale al file su disco.
printf 'lavoro in staging\n' >c.h
run git shared update
expect_rc "modifica in staging e su disco: codice 1" 1
expect_eq "modifica in staging e su disco: conservata" "$(git show :c.h)" "lavoro in staging"

# -f sovrascrive comunque (REQ-F-010).
run git shared update -f
expect_rc "update -f: codice 0" 0
expect_eq "update -f: versione applicata" "$(cat c.h)" "v5"

done_testing
