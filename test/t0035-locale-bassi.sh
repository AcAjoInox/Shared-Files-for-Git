#!/bin/sh
# t0035-locale-bassi.sh - i problemi di gravita' bassa del locale
# (report/bug-aperti.md §6): .gitshared non condivisibile (B-36), limite
# dei peer (B-38), revert in staging protetto (B-43), sorgente non
# allineata (B-44), cartella degli hook (C-04), path con un a capo (C-08),
# attesa su due righe (C-09), PATH su Windows (C-10), maiuscole su Windows
# (C-12), path assoluti (C-13), messaggi dei peer e di ls-files (C-14),
# intestazione di .gitshared (C-18), link interrotto (C-21), argomenti dei
# testi della tabella dei messaggi (C-15, C-16).

. "$(dirname "$0")/lib.sh"

make_repo A
cd "$TRASH/A" || exit 1
put p.h v1
put q.h q1
git shared set -c p.h >/dev/null
git shared set -c q.h >/dev/null
ID=$(shared_id p.h)

# --- C-18: .gitshared nasce gia' con l'intestazione, senza file temporanei --------
expect_fail "C-18: nessun .gitshared.new rimasto" test -e .gitshared.new
case "$(sed -n 1p .gitshared)" in
'#'*) ok "C-18: .gitshared comincia con l'intestazione" ;;
*) not_ok "C-18: .gitshared comincia con l'intestazione" "$(sed -n 1p .gitshared)" ;;
esac

# --- B-36: .gitshared non si condivide ---------------------------------------------
run git shared set .gitshared
expect_rc "B-36: set .gitshared: codice 1" 1
expect_err "B-36: lo dice" "cannot be shared itself"
expect_eq "B-36: nessuna attesa" "$(git config --get-regexp '^shared\..*\.pending' | wc -l | tr -d ' ')" "0"

# --- C-13: path assoluti dentro la repo, come per set -----------------------------
ABS=$(pwd -W 2>/dev/null || pwd)
run git shared update "$ABS/p.h"
expect_rc "C-13: update con il path assoluto: codice 0" 0
expect_out "C-13: update con il path assoluto: il file" "p.h"
run git shared log -n 1 "$ABS/p.h"
expect_rc "C-13: log con il path assoluto: codice 0" 0
run git shared update "$TRASH/altrove/p.h"
expect_rc "C-13: path assoluto fuori dalla repo: codice 1" 1

# --- C-14: path fuori dalla repo per set: errore, non "cannot run" ----------------
run git shared set ../fuori.h
expect_rc "C-14: set fuori dalla repo: codice 1" 1
expect_err "C-14: set fuori dalla repo: lo dice" "is not a path of this repository"

# --- B-44: link avvisa se la sorgente non e' l'ultima versione --------------------
make_repo B
cd "$TRASH/B" || exit 1
put x.txt x
run git shared link -c ../A/p.h
expect_rc "B-44: sorgente allineata: codice 0" 0
expect_err_none() { # stderr senza il testo
	case "$ERR" in
	*"$2"*) not_ok "$1" "stderr: $ERR" ;;
	*) ok "$1" ;;
	esac
}
expect_err_none "B-44: sorgente allineata: nessun avviso" "not the latest published version"
cd "$TRASH/A" || exit 1
printf 'locale\n' >>q.h
make_repo B2
cd "$TRASH/B2" || exit 1
put x.txt x
run git shared link -c ../A/q.h
expect_rc "B-44: sorgente con una modifica: codice 0" 0
expect_err "B-44: sorgente con una modifica: avviso" "is not the latest published version"
expect_eq "B-44: scritta l'ultima versione" "$(cat q.h)" "q1"
(cd "$TRASH/A" && git checkout -q -- q.h)

# --- C-12: su Windows il nome della sorgente non distingue le maiuscole -----------
make_repo B3
cd "$TRASH/B3" || exit 1
put x.txt x
case "$(uname -s)" in
MINGW* | MSYS* | CYGWIN*)
	run git shared link -c ../A/P.H
	expect_rc "C-12: link ../A/P.H su Windows: codice 0" 0
	expect_ok "C-12: destinazione con il path dichiarato" test -n "$(git ls-files p.h)"
	;;
*)
	skip "C-12: link ../A/P.H su Windows" "solo Windows"
	skip "C-12: destinazione con il path dichiarato" "solo Windows"
	;;
esac

# --- C-21: un antenato della destinazione e' un file ------------------------------
make_repo C
cd "$TRASH/C" || exit 1
put a x
run git shared link ../A/p.h a/p.h
expect_rc "C-21: destinazione sotto un file: codice 1" 1
expect_err "C-21: lo dice" "a is a file, not a folder"
expect_fail "C-21: nessun .gitshared" test -e .gitshared
expect_eq "C-21: nessuna attesa" "$(pending_op "$ID")" ""
run git shared status
expect_no_out "C-21: nessun file shared" "p.h"

# --- B-36: link con destinazione .gitshared ---------------------------------------
run git shared link ../A/p.h .gitshared
expect_rc "B-36: link su .gitshared: codice 1" 1
expect_err "B-36: link su .gitshared: lo dice" "cannot be shared itself"

# --- B-38: al piu' 32 peer, controllato prima di scrivere -------------------------
make_repo M
cd "$TRASH/M" || exit 1
put m.h m
git shared set -c m.h >/dev/null
IDM=$(shared_id m.h)
k=1
while [ "$k" -le 32 ]; do
	git config --add "shared.$IDM.peer" "$TRASH/finto$k"
	k=$((k + 1))
done
make_repo D
cd "$TRASH/D" || exit 1
put x.txt x
run git shared link ../M/m.h
expect_rc "B-38: gruppo pieno: codice 1" 1
expect_err "B-38: lo dice" "too many peers (at most 32)"
expect_eq "B-38: nessuna attesa" "$(pending_op "$IDM")" ""
expect_fail "B-38: nessun .gitshared" test -e .gitshared

# --- B-43: un revert in staging a una versione vecchia e' protetto ----------------
cd "$TRASH/A" || exit 1
put p.h v2
git shared publish >/dev/null 2>&1
cd "$TRASH/B" || exit 1
git shared update -c p.h >/dev/null 2>&1
expect_eq "B-43: B alla v2" "$(cat p.h)" "v2"
cd "$TRASH/A" || exit 1
put p.h v3
git shared publish >/dev/null 2>&1
cd "$TRASH/B" || exit 1
printf 'v1\n' >p.h
git add p.h
run git shared update p.h
expect_rc "B-43: revert alla v1 in staging: update rifiuta" 1
expect_err "B-43: modifiche locali" "has local changes"
expect_eq "B-43: il revert resta" "$(cat p.h)" "v1"
run git shared update -f p.h
expect_rc "B-43: con -f: codice 0" 0
expect_eq "B-43: con -f arriva la v3" "$(cat p.h)" "v3"
git commit -q -m v3 2>/dev/null
# un update applicato e non committato resta "da committare" (D-076)
cd "$TRASH/A" || exit 1
put p.h v4
git shared publish >/dev/null 2>&1
cd "$TRASH/B" || exit 1
git shared update p.h >/dev/null 2>&1
run git shared update p.h
expect_rc "D-076: update ripetuto: codice 0" 0
expect_out "D-076: gia' applicata" "applied but not committed"
git commit -q -m v4 2>/dev/null

# --- C-14: un peer che c'e' ma non ha il file -------------------------------------
make_repo Z
cd "$TRASH/B" || exit 1
git config --add "shared.$ID.peer" "$TRASH/Z"
run git shared update --scan
expect_err "C-14: peer senza il file: lo dice" "peer $TRASH/Z does not have this file"
expect_err_none "C-14: peer senza il file: non 'not reachable'" "peer $TRASH/Z not reachable"
git config --unset "shared.$ID.peer" "$TRASH/Z"

# --- C-08: path con un a capo in .gitshared: voce non valida, gli altri a posto ---
cd "$TRASH/A" || exit 1
cp .gitshared "$TRASH/gitshared.bak"
git config -f .gitshared shared.rotto.id 0123456789abcdef0123456789abcdef
git config -f .gitshared shared.rotto.path "$(printf 'x\ny.h')"
run git shared status
expect_out "C-08: p.h a posto" "p.h"
expect_out "C-08: q.h a posto" "q.h"
expect_no_out "C-08: nessuno stato sbagliato" "diverged"
cp "$TRASH/gitshared.bak" .gitshared

# --- C-09: un'attesa scritta su due righe si chiude -------------------------------
put r.h r1
git shared set r.h >/dev/null 2>&1
IDR=$(shared_id r.h)
V=$(git config "shared.$IDR.pending")
git config --add "shared.$IDR.pending" "$V"
expect_eq "C-09: due righe" "$(git config --get-all "shared.$IDR.pending" | grep -c '^set$')" "2"
git commit -q -m "set r.h" 2>/dev/null
git shared status >/dev/null 2>&1
expect_eq "C-09: attesa chiusa, nessuna riga" "$(git config --get-all "shared.$IDR.pending")" ""

# --- C-04: senza la cartella degli hook si crea -----------------------------------
make_repo H
cd "$TRASH/H" || exit 1
rm -rf .git/hooks
put h.h h
run git shared set -c h.h
expect_rc "C-04: set senza cartella degli hook: codice 0" 0
expect_err_none "C-04: nessun avviso sugli hook" "hook"
expect_ok "C-04: post-merge installato" grep -q ">>> git shared >>>" .git/hooks/post-merge
expect_ok "C-04: post-checkout installato" grep -q ">>> git shared >>>" .git/hooks/post-checkout

# --- C-10: su Windows, senza PATH git non si cerca nella cartella corrente -------
case "$(uname -s)" in
MINGW* | MSYS* | CYGWIN*)
	cp "$BUILD_DIR/git-shared.exe" "$TRASH/H/git.exe"
	env -u PATH "$BUILD_DIR/git-shared.exe" status >"$TRASH/.out" 2>"$TRASH/.err"
	RC=$?
	ERR=$(cat "$TRASH/.err")
	if [ "$RC" -ne 0 ]; then
		ok "C-10: senza PATH: errore"
	else
		not_ok "C-10: senza PATH: errore" "codice 0"
	fi
	expect_err_none "C-10: il git.exe della cartella non e' partito" "unknown subcommand"
	rm -f "$TRASH/H/git.exe"
	;;
*)
	skip "C-10: senza PATH: errore" "solo Windows"
	skip "C-10: il git.exe della cartella non e' partito" "solo Windows"
	;;
esac

# --- C-15, C-16: argomenti dei testi della tabella dei messaggi -------------------
SRC_DIR=$(cd "$TEST_DIR/.." && pwd)
run awk -f "$TEST_DIR/msgcheck.awk" "$SRC_DIR/src/msg.c" "$SRC_DIR"/src/*.c "$SRC_DIR"/utils/*.c
expect_rc "C-16: ogni chiamata ha gli argomenti del suo testo" 0
case "$OUT" in
"checked "[1-9][0-9][0-9]*) ok "C-16: controllate le chiamate (piu' di 100)" ;;
*) not_ok "C-16: controllate le chiamate (piu' di 100)" "$OUT" ;;
esac
C15=$(grep -n '"shared files"\|? "publish" : "update"' "$SRC_DIR"/src/*.c | grep -v '^.*msg\.c:')
expect_eq "C-15: testi fuori dalla tabella" "$C15" ""

done_testing
