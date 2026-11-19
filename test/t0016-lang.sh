#!/bin/sh
# t0016-lang.sh - git shared lang e manuale in italiano (REQ-NF-008,
# REQ-F-019, D-088).

. "$(dirname "$0")/lib.sh"

cd "$TRASH" || exit 1

# --- argomenti e lingua di default ----------------------------------------------
run git shared lang
expect_rc "senza argomenti: codice 0" 0
expect_eq "default: inglese" "$OUT" "en"
run git shared lang xx
expect_rc "lingua sconosciuta: codice 129" 129
expect_err "lingua sconosciuta: errore" "unknown language: 'xx' (use en or it)"
run git shared lang it en
expect_rc "due argomenti: codice 129" 129
run git shared lang -h
expect_out "uso: breve" "Show or choose the language of the manual"

# --- l'aiuto generale -h spiega dove sta il manuale e come cambiare lingua -----
run git shared -h
expect_out "-h: help" "git shared help                overview and list of commands"
expect_out "-h: --help" "git shared --help              the same overview, opened by git"
expect_out "-h: pagina di un comando" "git shared <command> --help    page of a command, for example"
expect_out "-h: esempio" "'git shared link --help'"
expect_out "-h: help <comando>" "git shared help <command>      the same page"
expect_out "-h: lingua" "In English or Italian (manuale in italiano):"
expect_out "-h: comando per l'italiano" "git shared lang it"

# --- italiano -------------------------------------------------------------------
run git shared lang it
expect_rc "lang it: codice 0" 0
expect_out "lang it: conferma" "Manual language: it."
expect_eq "lang it: nella config globale" "$(git config --global shared.lang)" "it"
expect_eq "lang it: letta" "$(git shared lang)" "it"
run git shared help
expect_out "help in italiano" "NOME"
expect_out "help in italiano: rimando ai comandi" "git shared <comando> --help"
for c in set link unlink mv status publish update branch prune relocate lang hook; do
	C=$(echo "$c" | tr '[:lower:]' '[:upper:]')
	run git shared "$c" --help
	expect_out "$c --help in italiano" "GIT-SHARED-$C(1)"
	expect_out "$c --help in italiano: sezioni" "SINTASSI"
	run git shared help "$c"
	expect_out "help $c in italiano" "DESCRIZIONE"
done
run git shared update -h
expect_out "-h resta l'uso breve in inglese" "usage: git shared update"

# Anche in una repo (shared.lang si legge da ogni livello di config).
make_repo r
cd "$TRASH/r" || exit 1
run git shared link --help
expect_out "in una repo: italiano" "collega un file condiviso da un altro repository"
cd "$TRASH" || exit 1

# --- di nuovo inglese -------------------------------------------------------------
git shared lang en >/dev/null
run git shared help
expect_out "lang en: inglese" "NAME"
expect_no_out "lang en: niente italiano" "NOME"

# --- pagina per git shared --help nella lingua scelta ------------------------------
"$BUILD_DIR/embed" -html "$TRASH/it.html" "$TEST_DIR/../help/it/git-shared.txt"
expect_ok "HTML italiano: lingua dichiarata" grep -q 'lang="it"' "$TRASH/it.html"
expect_ok "HTML italiano: testo" grep -q "PER INIZIARE" "$TRASH/it.html"
"$BUILD_DIR/embed" -man "$TRASH/it.1" "$TEST_DIR/../help/it/git-shared.txt"
expect_ok "man italiano" grep -q "^NOME" "$TRASH/it.1"

done_testing
