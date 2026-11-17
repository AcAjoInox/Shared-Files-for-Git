#!/bin/sh
# t0014-avvisi.sh - nessun silenzio: file sospeso su tutti i branch
# (reportIdea B-21) e publish/update su un file non utilizzabile (B-37).
# D-085.

. "$(dirname "$0")/lib.sh"

# --- B-21: branch rinominato ---------------------------------------------------------
make_repo r
cd "$TRASH/r" || exit 1
put o.txt o
put c.h v1
git shared set -c c.h >/dev/null
git branch dev

run git shared status
expect_no_out "al suo posto: nessuna nota" "note"

git switch -q dev
run git shared status
expect_out "altro branch esistente: sospeso" "suspended on this branch"
expect_no_out "altro branch esistente: nessuna nota (main esiste)" "note"
git switch -q main

git branch -m main master
run git shared status
expect_out "rinomina: sospeso" "suspended on this branch (aligned)"
expect_out "rinomina: nota con il rimedio" \
	"note   none of its active branches exists (main): git shared branch c.h -a <branch>"

# Gli hook avvisano (prima i file sospesi non avevano nessun avviso).
git switch -q dev 2>"$TRASH/hook.err"
expect_ok "rinomina: avviso dell'hook dopo il checkout" \
	grep -q "c.h: none of its active branches exists (main): run 'git shared branch c.h -a <branch>'" "$TRASH/hook.err"
git switch -q master 2>/dev/null

run git shared update
expect_err "rinomina: update avvisa" "c.h: suspended on this branch"
expect_err "rinomina: update dice il rimedio" "none of its active branches exists (main)"
run git shared publish
expect_err "rinomina: publish dice il rimedio" "none of its active branches exists (main)"

# Rimedio: niente piu' note ne' avvisi.
git shared branch c.h -a master -rm main >/dev/null
run git shared status
expect_no_out "corretto: nessuna nota" "note"
expect_out "corretto: allineato" "aligned"
git switch -q dev 2>"$TRASH/hook.err"
expect_eq "corretto: nessun avviso dell'hook" "$(cat "$TRASH/hook.err")" ""
git switch -q master

# Un branch futuro accanto a uno esistente non e' un problema.
git shared branch c.h -a release >/dev/null 2>&1
git switch -q dev
run git shared status
expect_no_out "branch futuro: nessuna nota" "note"
git switch -q master

# --- B-37: publish/update su un file in attesa o scollegato -------------------------------
make_repo p
cd "$TRASH/p" || exit 1
put o.txt o
put a.h a
git shared set -c a.h >/dev/null
put n.h n
git shared set n.h >/dev/null

run git shared publish n.h
expect_rc "in attesa, publish <file>: codice 1" 1
expect_err "in attesa, publish <file>: perche'" "'n.h' has a pending operation"
run git shared update n.h
expect_rc "in attesa, update <file>: codice 1" 1
expect_err "in attesa, update <file>: perche'" "'n.h' has a pending operation"
run git shared publish
expect_rc "in attesa, publish: codice 0" 0
expect_out "in attesa, publish: riga per il file saltato" "n.h: pending commit, nothing to do"
expect_out "in attesa, publish: l'altro file si pubblica" "a.h: no peers to send it to"
run git shared update
expect_out "in attesa, update: riga per il file saltato" "n.h: pending commit, nothing to do"

git clone -q "$TRASH/p" "$TRASH/c"
cd "$TRASH/c" || exit 1
run git shared update a.h
expect_rc "scollegato, update <file>: codice 1" 1
expect_err "scollegato, update <file>: come uscirne" \
	"'a.h' is not linked here: run 'git shared link' first"
run git shared publish a.h
expect_rc "scollegato, publish <file>: codice 1" 1
run git shared update
expect_rc "scollegato, update: codice 0" 0
expect_out "scollegato, update: riga per il file saltato" "a.h: unlinked, nothing to do"

done_testing
