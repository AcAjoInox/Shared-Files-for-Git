# lib.sh - funzioni comuni dei test di git shared.
#
# Uso, in testa a ogni t*.sh:   . "$(dirname "$0")/lib.sh"
#
# Ogni test gira in una cartella temporanea ($TRASH), cancellata alla fine,
# con l'eseguibile di build/ in testa al PATH e senza la configurazione git
# dell'utente o del sistema. Output nel formato TAP ("ok N - ...").

TEST_DIR=$(cd "$(dirname "$0")" && pwd)
BUILD_DIR=$(cd "$TEST_DIR/../build" && pwd) || {
	echo "Bail out! manca build/: lanciare 'make'"
	exit 1
}
PATH="$BUILD_DIR:$PATH"
export PATH

TRASH=$(mktemp -d "${TMPDIR:-/tmp}/git-shared-test.XXXXXX")
trap 'rm -rf "$TRASH"' EXIT

# Ambiente isolato e riproducibile.
HOME="$TRASH"
# git non risale oltre la cartella di $TRASH: se TMPDIR sta dentro una repo,
# le cartelle "fuori da una repo" dei test restano tali. Su Windows il path
# in forma C:/..., quella che legge git.exe (pwd -W, solo in MSYS).
GIT_CEILING_DIRECTORIES=$(cd "$(dirname "$TRASH")" &&
	{ pwd -W 2>/dev/null || pwd; })
export GIT_CEILING_DIRECTORIES
GIT_CONFIG_NOSYSTEM=1
GIT_AUTHOR_NAME="Test Author"
GIT_AUTHOR_EMAIL="author@example.com"
GIT_COMMITTER_NAME="Test Committer"
GIT_COMMITTER_EMAIL="committer@example.com"
export HOME GIT_CONFIG_NOSYSTEM GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL \
	GIT_COMMITTER_NAME GIT_COMMITTER_EMAIL

n_test=0
n_fail=0

ok() {
	n_test=$((n_test + 1))
	echo "ok $n_test - $1"
}

not_ok() {
	n_test=$((n_test + 1))
	n_fail=$((n_fail + 1))
	echo "not ok $n_test - $1"
	shift
	for line in "$@"; do
		echo "#   $line"
	done
}

# skip <descrizione> <motivo>: test che qui non ha senso (direttiva TAP)
skip() {
	n_test=$((n_test + 1))
	echo "ok $n_test - $1 # SKIP $2"
}

# has_exec_bit: il filesystem conserva il bit di esecuzione. Su Windows no:
# Git Bash considera eseguibile ogni file che comincia con "#!".
has_exec_bit() {
	printf '#!/bin/sh\n' >"$TRASH/.exec"
	chmod 644 "$TRASH/.exec"
	! [ -x "$TRASH/.exec" ]
}

# run <comando...>: esegue e salva codice di uscita, stdout e stderr
# in $RC, $OUT, $ERR.
run() {
	"$@" >"$TRASH/.out" 2>"$TRASH/.err"
	RC=$?
	OUT=$(cat "$TRASH/.out")
	ERR=$(cat "$TRASH/.err")
}

# expect_rc <descrizione> <codice atteso>
expect_rc() {
	if [ "$RC" -eq "$2" ]; then
		ok "$1"
	else
		not_ok "$1" "codice di uscita $RC, atteso $2" "stderr: $ERR"
	fi
}

# expect_out <descrizione> <testo>: stdout contiene il testo
expect_out() {
	case "$OUT" in
	*"$2"*) ok "$1" ;;
	*) not_ok "$1" "stdout non contiene: $2" "stdout: $OUT" ;;
	esac
}

# expect_err <descrizione> <testo>: stderr contiene il testo
expect_err() {
	case "$ERR" in
	*"$2"*) ok "$1" ;;
	*) not_ok "$1" "stderr non contiene: $2" "stderr: $ERR" ;;
	esac
}

# expect_no_out <descrizione> <testo>: stdout NON contiene il testo
expect_no_out() {
	case "$OUT" in
	*"$2"*) not_ok "$1" "stdout contiene: $2" ;;
	*) ok "$1" ;;
	esac
}

# make_repo <nome>: repo git vuota in $TRASH/<nome>, branch iniziale main
make_repo() {
	git init -q -b main "$TRASH/$1"
}

# expect_eq <descrizione> <ottenuto> <atteso>
expect_eq() {
	if [ "$2" = "$3" ]; then
		ok "$1"
	else
		not_ok "$1" "ottenuto: '$2'" "atteso:   '$3'"
	fi
}

# expect_ok <descrizione> <comando...>: il comando termina con 0
expect_ok() {
	desc=$1
	shift
	if "$@" >/dev/null 2>&1; then
		ok "$desc"
	else
		not_ok "$desc" "comando fallito: $*"
	fi
}

# expect_fail <descrizione> <comando...>: il comando termina con errore
expect_fail() {
	desc=$1
	shift
	if "$@" >/dev/null 2>&1; then
		not_ok "$desc" "comando riuscito: $*"
	else
		ok "$desc"
	fi
}

# put <file> <contenuto>: scrive il file e lo committa. L'output degli
# hook (avvisi, pubblicazione) e' scartato: lo verifica t0006-hooks.sh.
put() {
	printf '%s\n' "$2" >"$1"
	git add -- "$1"
	git commit -q -m "$1: $2" 2>/dev/null
}

# shared_id <nome>: id della voce <nome> in .gitshared (repo corrente)
shared_id() {
	git config -f .gitshared "shared.$1.id"
}

# pending_op <id> [<repo>]: operazione in attesa, la prima riga della
# chiave shared.<id>.pending ("" se nessuna, D-113)
pending_op() {
	git -C "${2:-.}" config "shared.$1.pending" 2>/dev/null | sed -n 1p
}

# pending_field <id> <campo>: campo dell'attesa (path, branch, remote,
# commit, source), una riga della chiave; la sorgente e' il resto (D-113)
pending_field() {
	case "$2" in
	path) _pf=2p ;;
	branch) _pf=3p ;;
	remote) _pf=4p ;;
	commit) _pf=5p ;;
	*) _pf='6,$p' ;;
	esac
	git config "shared.$1.pending" 2>/dev/null | sed -n "$_pf"
}

# done_testing: piano TAP; codice di uscita 1 se un test e' fallito
done_testing() {
	echo "1..$n_test"
	[ "$n_fail" -eq 0 ]
}
