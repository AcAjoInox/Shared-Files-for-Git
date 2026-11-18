#!/bin/sh
# t0018-completamento.sh - set, link e unlink si completano al commit che li
# conferma: il post-commit c'e' solo finche' un'attesa e' aperta e si toglie
# da solo; link completa la sorgente rimasta in attesa (REQ-F-016,
# REQ-F-023, D-100).

. "$(dirname "$0")/lib.sh"

top() {
	git -C "$TRASH/$1" rev-parse --show-toplevel
}

has_block() {
	grep -q ">>> git shared >>>" ".git/hooks/$1" 2>/dev/null
}

# commit_no_hooks <args...>: commit senza hook (un client che li salta)
commit_no_hooks() {
	git -c core.hooksPath="$TRASH/nessun-hook" commit "$@"
}

# --- set: commit intermedio, poi quello che conferma ---------------------------
make_repo A
cd "$TRASH/A" || exit 1
put f.h v1
git shared set f.h >/dev/null
ID=$(shared_id f.h)
expect_ok "set: blocco nel post-commit" has_block post-commit
printf 'x\n' >x.txt
git add x.txt
run git commit -q -m "solo x" -- x.txt
expect_eq "commit senza .gitshared: nessun output" "$OUT$ERR" ""
expect_eq "commit senza .gitshared: ancora in attesa" "$(git config "shared.$ID.pending")" "set"
expect_ok "commit senza .gitshared: il blocco resta" has_block post-commit
run git commit -q -m "share f.h"
expect_eq "commit di conferma: nessun output" "$OUT$ERR" ""
expect_ok "commit di conferma: catena" git rev-parse -q --verify "refs/shared/$ID"
expect_fail "commit di conferma: attesa chiusa" git config "shared.$ID.pending"
expect_fail "commit di conferma: post-commit tolto" test -e .git/hooks/post-commit
put altro.txt a
expect_fail "commit dopo: nessun post-commit" test -e .git/hooks/post-commit

# --- set -c: nessun blocco ------------------------------------------------------
put g.h g
run git shared set -c g.h
expect_rc "set -c: codice 0" 0
expect_fail "set -c: nessun post-commit" test -e .git/hooks/post-commit
expect_fail "set -c: completato" git config "shared.$(shared_id g.h).pending"

# --- git restore e commit: attesa scartata, blocco tolto ------------------------
put h.h h
git shared set h.h >/dev/null
ID_H=$(shared_id h.h)
git checkout -q HEAD -- .gitshared
put y.txt y
expect_fail "restore + commit: attesa scartata" git config "shared.$ID_H.pending"
expect_fail "restore + commit: nessuna catena" git rev-parse -q --verify "refs/shared/$ID_H"
expect_fail "restore + commit: post-commit tolto" test -e .git/hooks/post-commit

# --- post-commit dell'utente: gira una volta, torna com'era -----------------------
# Il blocco si toglie mentre la shell esegue l'hook: il resto dell'hook deve
# girare per intero e una volta sola (file nuovo e rinomina, D-100). Con
# bash, che prima di lanciare un comando riporta il file alla posizione in
# cui e' arrivato e poi lo rilegge da li': riscritto sul posto, il resto
# si leggerebbe spostato.
make_repo U
cd "$TRASH/U" || exit 1
put u.h u
cat >.git/hooks/post-commit <<'HOOK'
#!/bin/bash
echo "post-utente: una riga abbastanza lunga da spostare tutto il resto" >&2
echo "seconda-utente" >&2
HOOK
chmod 700 .git/hooks/post-commit
cp .git/hooks/post-commit "$TRASH/U-orig"
git shared set u.h >/dev/null
ID_U=$(shared_id u.h)
expect_ok "hook dell'utente: blocco inserito" has_block post-commit
run git commit -q -m "share u.h"
expect_eq "hook dell'utente: prima riga una volta" \
	"$(printf '%s\n' "$ERR" | grep -c 'post-utente')" 1
expect_eq "hook dell'utente: seconda riga una volta" \
	"$(printf '%s\n' "$ERR" | grep -c 'seconda-utente')" 1
expect_ok "hook dell'utente: completato" git rev-parse -q --verify "refs/shared/$ID_U"
expect_ok "hook dell'utente: contenuto ripristinato" cmp -s .git/hooks/post-commit "$TRASH/U-orig"
if has_exec_bit; then
	expect_eq "hook dell'utente: permessi invariati" \
		"$(ls -l .git/hooks/post-commit | cut -c1-10)" "-rwx------"
else
	skip "hook dell'utente: permessi invariati" "niente bit di esecuzione su questo filesystem"
fi
expect_fail "hook dell'utente: nessun file temporaneo" test -e .git/hooks/post-commit.git-shared-new

# --- link: completato al commit, con le versioni pubblicate nel frattempo --------
make_repo S
cd "$TRASH/S" || exit 1
put s.h v1
git shared set -c s.h >/dev/null
ID_S=$(shared_id s.h)
make_repo D
cd "$TRASH/D" || exit 1
put d.txt d
git shared link ../S/s.h >/dev/null
expect_ok "link: blocco nel post-commit" has_block post-commit
# S pubblica prima che D confermi: non conosce ancora D, non lo avvisa.
cd "$TRASH/S" || exit 1
put s.h v2
git shared publish >/dev/null
cd "$TRASH/D" || exit 1
run git commit -q -m "link s.h"
expect_eq "link al commit: nessun output" "$OUT$ERR" ""
expect_eq "link al commit: S conosce D" "$(git -C ../S config --get-all "shared.$ID_S.peer")" "$(top D)"
expect_fail "link al commit: attesa chiusa" git config "shared.$ID_S.pending"
expect_fail "link al commit: post-commit tolto" test -e .git/hooks/post-commit
run git shared status
expect_out "link al commit: la versione persa arriva" "new version to apply"

# --- unlink dell'ultimo file: .gitshared sparisce, il commit completa -------------
git shared update -c >/dev/null
run git shared unlink s.h
expect_ok "unlink: blocco nel post-commit" has_block post-commit
run git commit -q -m "unlink s.h"
expect_fail "unlink al commit: .gitshared tolto" test -e .gitshared
expect_eq "unlink al commit: S dimentica D" "$(git -C ../S config --get-all "shared.$ID_S.peer")" ""
expect_fail "unlink al commit: sezione locale tolta" git config "shared.$ID_S.pending"
expect_fail "unlink al commit: post-commit tolto" test -e .git/hooks/post-commit

# --- hook saltato: link completa il set della sorgente ----------------------------
make_repo S2
cd "$TRASH/S2" || exit 1
put t.h t
git shared set t.h >/dev/null
ID_T=$(shared_id t.h)
commit_no_hooks -q -m "share t.h"
expect_eq "hook saltato: set ancora in attesa" "$(git config "shared.$ID_T.pending")" "set"
make_repo D2
cd "$TRASH/D2" || exit 1
put z.txt z
run git shared link ../S2/t.h
expect_rc "sorgente in attesa: link riesce" 0
expect_eq "sorgente in attesa: nessun errore" "$ERR" ""
expect_fail "sorgente in attesa: completata" git -C ../S2 config "shared.$ID_T.pending"
expect_ok "sorgente in attesa: catena nella sorgente" \
	git -C ../S2 rev-parse -q --verify "refs/shared/$ID_T"
expect_fail "sorgente in attesa: post-commit della sorgente tolto" \
	test -e "$TRASH/S2/.git/hooks/post-commit"
git commit -q -m "link t.h"
expect_eq "sorgente in attesa: poi peer" "$(git -C ../S2 config --get-all "shared.$ID_T.peer")" "$(top D2)"

# --- sorgente con il set non committato: errore, l'attesa resta -------------------
make_repo S3
cd "$TRASH/S3" || exit 1
put q.h q
git shared set q.h >/dev/null
ID_Q=$(shared_id q.h)
make_repo D3
cd "$TRASH/D3" || exit 1
put z.txt z
run git shared link ../S3/q.h
expect_rc "set non committato: link rifiutato" 1
expect_err "set non committato: perche'" \
	"'../S3/q.h' is not shared yet in $(top S3): commit .gitshared there, then run link again"
case "$ERR" in
*"not reachable"*) not_ok "set non committato: nessun falso irraggiungibile" "$ERR" ;;
*) ok "set non committato: nessun falso irraggiungibile" ;;
esac
expect_eq "set non committato: attesa della sorgente intatta" \
	"$(git -C ../S3 config "shared.$ID_Q.pending")" "set"
expect_fail "set non committato: niente nella destinazione" test -e .gitshared

# --- hook saltato: link da una repo il cui link e' ancora in attesa -------------
make_repo S4
cd "$TRASH/S4" || exit 1
put a.h a
git shared set -c a.h >/dev/null
ID_A=$(shared_id a.h)
make_repo D4
cd "$TRASH/D4" || exit 1
put o.txt o
git shared link ../S4/a.h >/dev/null
commit_no_hooks -q -m "link a.h"
make_repo E4
cd "$TRASH/E4" || exit 1
put o.txt o
run git shared link -c ../D4/a.h
expect_rc "link in attesa nella sorgente: link riesce" 0
expect_fail "link in attesa nella sorgente: completato" git -C ../D4 config "shared.$ID_A.pending"
expect_ok "link in attesa nella sorgente: S4 conosce D4" \
	sh -c "git -C '$TRASH/S4' config --get-all shared.$ID_A.peer | grep -qx '$(top D4)'"
expect_ok "link in attesa nella sorgente: S4 conosce E4" \
	sh -c "git -C '$TRASH/S4' config --get-all shared.$ID_A.peer | grep -qx '$(top E4)'"

# --- post-commit non di shell: avviso, si completa al comando dopo ---------------
make_repo NS
cd "$TRASH/NS" || exit 1
put n.h n
printf '#!/usr/bin/env python3\nimport sys\nsys.exit(0)\n' >.git/hooks/post-commit
cp .git/hooks/post-commit "$TRASH/NS-orig"
run git shared set n.h
expect_err "post-commit non sh: avviso" \
	"post-commit is not a shell script: left as it is (the pending operation completes at the next 'git shared' command, not at the commit)"
expect_ok "post-commit non sh: intatto" cmp -s .git/hooks/post-commit "$TRASH/NS-orig"
commit_no_hooks -q -m "share n.h"
run git shared status
expect_out "post-commit non sh: il comando dopo completa" "aligned"

done_testing
