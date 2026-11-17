#!/bin/sh
# t0002-status.sh - status di M0: elenco delle voci di .gitshared
# (REQ-F-012 parziale, REQ-F-021, §6.3).

. "$(dirname "$0")/lib.sh"

ID_A=0f8c3e5a9b2d4c71a6e0d9b8c7f61234
ID_B=11112222333344445555666677778888

# add_entry <repo> <nome> <id> <path>
add_entry() {
	git config -f "$1/.gitshared" "shared.$2.id" "$3"
	git config -f "$1/.gitshared" "shared.$2.path" "$4"
}

mkdir "$TRASH/norepo"
cd "$TRASH/norepo" || exit 1
run git shared status
expect_rc "fuori da una repo: codice 128" 128
expect_err "fuori da una repo: errore" "not a git repository"

make_repo empty
cd "$TRASH/empty" || exit 1
run git shared status
expect_rc "repo senza .gitshared: codice 0" 0
expect_out "repo senza .gitshared: nessun file" "No shared files"

run git shared status extra
expect_rc "argomento in piu': codice 129" 129
expect_err "argomento in piu': errore" "too many arguments"

make_repo two
add_entry "$TRASH/two" common.h "$ID_A" include/common.h
add_entry "$TRASH/two" "tools.sh" "$ID_B" scripts/tools.sh
cd "$TRASH/two" || exit 1
run git shared status
expect_rc "due voci: codice 0" 0
expect_out "due voci: primo nome" "common.h"
expect_out "due voci: primo path" "include/common.h"
expect_out "due voci: primo id" "$ID_A"
expect_out "due voci: secondo nome (con punto)" "tools.sh"
expect_out "due voci: secondo path" "scripts/tools.sh"

mkdir -p "$TRASH/two/sub/dir"
cd "$TRASH/two/sub/dir" || exit 1
run git shared status
expect_rc "da una sottocartella: codice 0" 0
expect_out "da una sottocartella: trova .gitshared alla radice" "$ID_A"

make_repo bad-entry
add_entry "$TRASH/bad-entry" good "$ID_A" good.h
add_entry "$TRASH/bad-entry" badid "XYZ" bad.h
add_entry "$TRASH/bad-entry" abspath "$ID_B" /etc/passwd
cd "$TRASH/bad-entry" || exit 1
run git shared status
expect_rc "voci non valide: codice 0" 0
expect_out "voci non valide: la voce buona c'e'" "good.h"
expect_no_out "voci non valide: id errato escluso" "bad.h"
expect_no_out "voci non valide: path assoluto escluso" "/etc/passwd"
expect_err "voci non valide: avviso per l'id" "invalid entry 'badid'"
expect_err "voci non valide: avviso per il path" "invalid entry 'abspath'"

make_repo malformed
printf '[shared\n' >"$TRASH/malformed/.gitshared"
cd "$TRASH/malformed" || exit 1
run git shared status
expect_rc ".gitshared malformato: codice 128" 128
expect_err ".gitshared malformato: riga indicata da git" "bad config line 1"
expect_err ".gitshared malformato: errore" "cannot read '.gitshared'"

# Path con spazi e caratteri non ASCII: passano a git senza shell (proc).
make_repo "dir con spazi è"
add_entry "$TRASH/dir con spazi è" common.h "$ID_A" include/common.h
cd "$TRASH/dir con spazi è" || exit 1
run git shared status
expect_rc "repo in un path con spazi e 'è': codice 0" 0
expect_out "repo in un path con spazi e 'è': voce letta" "$ID_A"

# git in un path non ASCII (reportIdea C-05): GIT_EXEC_PATH, che git passa
# all'estensione, punta a una cartella "git-José". Su Windows getenv la
# darebbe nella code page ANSI e nessun comando git partirebbe. Si lancia
# git-shared direttamente: git imposterebbe GIT_EXEC_PATH da se'.
EXEC_DIR=$(git --exec-path)
EXEC_LINK="$TRASH/git-José"
case "$(uname -s)" in
MINGW* | MSYS* | CYGWIN*)
	cmd //c mklink //J "$(cygpath -w "$EXEC_LINK")" "$(cygpath -w "$EXEC_DIR")" >/dev/null
	EXEC_ENV=$(cygpath -m "$EXEC_LINK")
	;;
*)
	ln -s "$EXEC_DIR" "$EXEC_LINK"
	EXEC_ENV=$EXEC_LINK
	;;
esac
expect_ok "git in 'git-José': la cartella c'e'" test -d "$EXEC_LINK"
run env GIT_EXEC_PATH="$EXEC_ENV" git-shared status
expect_rc "git in 'git-José': codice 0" 0
expect_out "git in 'git-José': voce letta" "$ID_A"
expect_eq "git in 'git-José': nessun errore" "$ERR" ""

done_testing
