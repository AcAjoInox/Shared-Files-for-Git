#!/bin/sh
# t0001-usage.sh - punto d'ingresso, -h, help, sottocomandi sconosciuti
# (REQ-F-018, REQ-F-019, REQ-NF-008, D-067).

. "$(dirname "$0")/lib.sh"

cd "$TRASH" || exit 1

run git shared
expect_rc "senza argomenti: codice 129" 129
expect_out "senza argomenti: stampa l'uso" "usage: git shared <command>"

run git shared -h
expect_rc "-h: codice 129" 129
expect_out "-h: elenca status" "status"
expect_out "-h: elenca help" "help"

run git shared status -h
expect_rc "status -h: codice 129" 129
expect_out "status -h: uso di status" "usage: git shared status"

run git shared help
expect_rc "help: codice 0" 0
expect_out "help: pagina generale" "GIT-SHARED(1)"
expect_out "help: rimanda alle pagine dei comandi" "git shared <command> --help"

run git shared help status
expect_rc "help status: codice 0" 0
expect_out "help status: pagina di status" "GIT-SHARED-STATUS(1)"

# Ogni comando ha la sua pagina (help/<comando>.txt), con --help e help.
for c in set link unlink status publish update branch prune relocate hook; do
	C=$(echo "$c" | tr '[:lower:]' '[:upper:]')
	run git shared "$c" --help
	expect_rc "$c --help: codice 0" 0
	expect_out "$c --help: pagina del comando" "GIT-SHARED-$C(1)"
	expect_out "$c --help: sintassi" "git shared $c"
	run git shared help
	expect_out "pagina generale: elenca $c" "    $c "
done
run git shared link --help
expect_eq "--help dopo il comando: nessun errore" "$ERR" ""

run git shared help nonexistent
expect_rc "help su argomento sconosciuto: codice 129" 129
expect_err "help su argomento sconosciuto: errore" "no help for 'nonexistent'"

run git-shared --help
expect_rc "--help diretto all'eseguibile: codice 0" 0
expect_out "--help diretto all'eseguibile: pagina generale" "GIT-SHARED(1)"

run git shared nonexistent
expect_rc "sottocomando sconosciuto: codice 129" 129
expect_err "sottocomando sconosciuto: errore" "unknown subcommand: 'nonexistent'"
expect_err "sottocomando sconosciuto: uso su stderr" "usage: git shared"

run git help -a
expect_out "git help -a elenca shared tra i comandi esterni" "shared"

done_testing
