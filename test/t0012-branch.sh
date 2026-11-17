#!/bin/sh
# t0012-branch.sh - git shared branch (REQ-F-022, D-057, D-083;
# reportIdea B-21).

. "$(dirname "$0")/lib.sh"

# active <id>: branch attivi locali, separati da spazi
active() {
	git config --get-all "shared.$1.branch" | tr '\n' ' '
}

# dflt <nome>: default in .gitshared, separati da spazi
dflt() {
	git config -f .gitshared --get-all "shared.$1.branch" | tr '\n' ' '
}

state() {
	git shared status 2>/dev/null | grep state
}

make_repo r
cd "$TRASH/r" || exit 1
put o.txt o
put c.h v1
git shared set -c c.h >/dev/null
ID=$(shared_id c.h)
git branch dev

# --- argomenti ----------------------------------------------------------------------
run git shared branch -h
expect_out "uso: breve" "Choose the branches where <path> is shared."
run git shared help
expect_out "manuale: branch" "Show or change the branches where a file is shared."
run git shared branch --add dev
expect_rc "--add senza path: codice 129" 129
run git shared branch c.h --add
expect_rc "--add senza valore: codice 129" 129
run git shared branch c.h --reset --add dev
expect_rc "--reset con --add: codice 129" 129
run git shared branch c.h --default
expect_rc "--default senza azione: codice 129" 129
run git shared branch c.h --add dev -c
expect_rc "-c senza --default: codice 129" 129
run git shared branch c.h o.txt
expect_rc "due path: codice 129" 129
run git shared branch c.h --boh
expect_rc "opzione sconosciuta: codice 129" 129
run git shared branch o.txt --add dev
expect_rc "file non shared: codice 1" 1
expect_err "file non shared: errore" "'o.txt' is not a shared file"

# --- elenco --------------------------------------------------------------------------
run git shared branch
expect_rc "elenco: codice 0" 0
expect_out "elenco: file" "c.h"
expect_out "elenco: attivi" "active   main (current)"
expect_out "elenco: default" "default  main (current)"

# --- --add / --remove: effetto immediato ------------------------------------------------
run git shared branch c.h --add dev
expect_rc "--add: codice 0" 0
expect_out "--add: messaggio" "c.h: active on 'dev'"
expect_eq "--add: elenco locale" "$(active "$ID")" "main dev "
expect_eq "--add: default non toccato" "$(dflt c.h)" "main "
expect_eq "--add: niente in staging" "$(git diff --cached --name-only)" ""
git switch -q dev
expect_eq "--add: su dev non piu' sospeso" "$(state)" "    state  aligned"
git switch -q main
git shared branch c.h --add dev >/dev/null
expect_eq "--add ripetuto: nessun doppione" "$(active "$ID")" "main dev "

run git shared branch c.h --add release
expect_rc "branch che non c'e': codice 0" 0
expect_err "branch che non c'e': avviso" "no branch named 'release' yet"
run git shared branch c.h
expect_out "branch che non c'e': segnato" "release (no such branch)"
git shared branch c.h --remove release >/dev/null
expect_eq "--remove: tolto" "$(active "$ID")" "main dev "
run git shared branch c.h --remove release
expect_rc "--remove di un branch assente: codice 1" 1
expect_err "--remove di un branch assente: errore" "c.h: 'release' is not in the list"

run git shared branch c.h --add 'release/*'
expect_rc "pattern: codice 1" 1
expect_err "pattern: non ancora" "branch patterns are not supported yet: 'release/*'"
run git shared branch c.h --add 'a..b'
expect_rc "nome non valido: codice 1" 1
expect_err "nome non valido: errore" "invalid branch name: 'a..b'"
expect_eq "nomi rifiutati: elenco intatto" "$(active "$ID")" "main dev "

run git shared branch c.h --remove dev --remove main
expect_rc "elenco vuoto: rifiutato" 1
expect_err "elenco vuoto: come smettere" "it must stay active on at least one branch (to stop sharing it: git shared unlink)"
expect_eq "elenco vuoto: niente scritto" "$(active "$ID")" "main dev "
run git shared branch c.h --remove main --add dev2
expect_rc "ordine libero: --remove prima di --add" 0
expect_eq "ordine libero: elenco" "$(active "$ID")" "dev dev2 "
expect_eq "solo dev attivo: su main sospeso" "$(state)" "    state  suspended on this branch (aligned)"

# --- --reset ------------------------------------------------------------------------------
run git shared branch c.h --reset
expect_rc "--reset: codice 0" 0
expect_out "--reset: messaggio" "c.h: active branches reset to the default"
expect_eq "--reset: copia del default" "$(active "$ID")" "main "
expect_eq "--reset: allineato" "$(state)" "    state  aligned"

# --- --default: in .gitshared, conta al commit -------------------------------------------
run git shared branch c.h --default --add dev
expect_rc "--default --add: codice 0" 0
expect_out "--default --add: messaggio" "c.h: 'dev' added to the default"
expect_out "--default --add: da committare" "Commit .gitshared to confirm."
expect_eq "--default --add: in .gitshared" "$(dflt c.h)" "main dev "
expect_eq "--default --add: in staging" "$(git diff --cached --name-only)" ".gitshared"
expect_eq "--default --add: elenco locale intatto" "$(active "$ID")" "main "
git commit -q -m "default dev"

printf 'altro\n' >o.txt
git add o.txt
run git shared branch c.h --default --remove dev -c
expect_rc "--default -c: codice 0" 0
expect_eq "--default -c: messaggio standard" "$(git log -1 --format=%s)" \
	"Change the default branches of c.h"
expect_eq "--default -c: solo .gitshared nel commit" \
	"$(git show --name-only --format= HEAD)" ".gitshared"
expect_eq "--default -c: o.txt resta in staging" "$(git diff --cached --name-only)" "o.txt"
git commit -q -m "o.txt"
git shared branch c.h --default --add dev -m "dev di default" >/dev/null
expect_eq "--default -m: messaggio" "$(git log -1 --format=%s)" "dev di default"

# --- il default si copia al link ------------------------------------------------------------
make_repo L
cd "$TRASH/L" || exit 1
put o.txt o
git shared link -c ../r/c.h >/dev/null
expect_eq "link: default della repo che si collega (il suo branch)" "$(active "$ID")" "main "

# --- file scollegato (clone) e attesa -----------------------------------------------------
git clone -q "$TRASH/r" "$TRASH/K"
cd "$TRASH/K" || exit 1
run git shared branch c.h
expect_out "scollegato: elenco" "active   (not linked here)"
run git shared branch c.h --add dev
expect_rc "scollegato: --add rifiutato" 1
expect_err "scollegato: come uscirne" "is not linked here: run 'git shared link' first"
run git shared branch c.h --default --add feat
expect_rc "scollegato: --default ammesso" 0
git restore --staged --worktree .gitshared
git shared link ../r/c.h >/dev/null
expect_eq "clone ricollegato: copia del default" "$(active "$ID")" "main dev "
run git shared branch c.h
expect_out "clone ricollegato: dev non c'e' qui" "dev (no such branch)"

make_repo P
cd "$TRASH/P" || exit 1
put o.txt o
put p.h v1
git shared set p.h >/dev/null
run git shared branch p.h --add dev
expect_rc "set in attesa: rifiutato" 1
expect_err "set in attesa: messaggio" "has a pending operation"

# --- branch rinominato (B-21): si vede e si corregge ---------------------------------------
cd "$TRASH/r" || exit 1
git branch -m main master
expect_eq "rinomina: sospeso" "$(state)" "    state  suspended on this branch (aligned)"
run git shared branch c.h
expect_out "rinomina: il vecchio nome e' segnato" "main (no such branch)"
git shared branch c.h --add master --remove main >/dev/null
expect_eq "rinomina: elenco corretto" "$(active "$ID")" "master "
expect_eq "rinomina: di nuovo allineato" "$(state)" "    state  aligned"

# --- piu' file ------------------------------------------------------------------------------
put d.h d
git shared set -c d.h >/dev/null
run git shared branch
expect_out "piu' file: c.h" "c.h"
expect_out "piu' file: d.h" "d.h"
run git shared branch d.h
expect_no_out "un file: solo d.h" "c.h"

done_testing
