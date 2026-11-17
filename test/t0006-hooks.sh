#!/bin/sh
# t0006-hooks.sh - hook: solo avvisi, nessun hook sui commit; si pubblica
# solo con publish (REQ-F-004, REQ-F-013, REQ-F-016, REQ-F-023, D-030,
# D-069, D-073, D-074).

. "$(dirname "$0")/lib.sh"

BREEF="$TEST_DIR/../breef.md"
WARN_HOOKS="post-checkout post-merge post-rewrite"

top() {
	git -C "$TRASH/$1" rev-parse --show-toplevel
}

# has_block <hook>: il blocco di git shared e' nell'hook
has_block() {
	grep -q ">>> git shared >>>" ".git/hooks/$1" 2>/dev/null
}

# --- A: hook installati da set ---------------------------------------------
make_repo A
cd "$TRASH/A" || exit 1
printf '#!/bin/sh\necho "hook-utente" >&2\nexit 0\n' >.git/hooks/pre-commit
chmod +x .git/hooks/pre-commit
put breef.md "$(cat "$BREEF")"
run git shared set breef.md
ID=$(shared_id breef.md)
for h in $WARN_HOOKS; do
	expect_ok "default: blocco in $h" has_block "$h"
done
expect_fail "default: nessun blocco nel post-commit" has_block post-commit
expect_fail "default: nessun blocco nel pre-commit" has_block pre-commit
expect_eq "default: hook dell'utente intatto" \
	"$(cat .git/hooks/pre-commit)" "$(printf '#!/bin/sh\necho "hook-utente" >&2\nexit 0')"
expect_eq "hook nuovo: inizia con #!/bin/sh" "$(head -n 1 .git/hooks/post-merge)" "#!/bin/sh"

# Commit senza hook di git shared: il set si completa al comando dopo.
run git commit -q -m "share breef.md"
expect_err "commit: l'hook dell'utente gira" "hook-utente"
expect_fail "default: dopo il commit la catena non c'e' ancora" \
	git rev-parse -q --verify "refs/shared/$ID"
run git shared status
expect_out "default: il comando successivo completa" "aligned"

put altro.h a
git shared set altro.h >/dev/null
expect_eq "secondo set: nessun blocco duplicato" "$(grep -c '>>> git shared >>>' .git/hooks/post-merge)" 1
git commit -q -m "share altro.h" 2>/dev/null

# --- B: link; il commit non pubblica (D-074) ---------------------------------
make_repo B
cd "$TRASH/B" || exit 1
put x.c x
git shared link -c ../A/breef.md >/dev/null
expect_eq "link -c: A conosce B" "$(git -C ../A config --get-all "shared.$ID.peer")" "$(top B)"
OLD_A=$(git -C ../A rev-parse "refs/shared/$ID")
printf 'riga di B\n' >>breef.md
run git commit -q -a -m "B modifica"
expect_eq "default: commit senza output di git shared" "$OUT$ERR" ""
expect_eq "default: A non riceve al commit" "$(git -C ../A rev-parse "refs/shared/$ID")" "$OLD_A"
run git shared status
expect_out "default: B da pubblicare" "to publish"
run git shared publish
expect_out "publish: inviata ad A" "sent to 1 of 1 peers"

# --- shared.autoPublish rimasto in config: ignorato (D-074) ------------------
git config shared.autoPublish true
run git shared hook install
expect_rc "hook install: codice 0" 0
expect_out "hook install: messaggio" "Hooks installed."
expect_fail "autoPublish in config: nessun blocco nel post-commit" has_block post-commit
OLD_A=$(git -C ../A rev-parse "refs/shared/$ID")
printf 'seconda riga di B\n' >>breef.md
run git commit -q -a -m "B modifica ancora"
expect_eq "autoPublish in config: commit senza output" "$OUT$ERR" ""
expect_eq "autoPublish in config: A non riceve al commit" \
	"$(git -C ../A rev-parse "refs/shared/$ID")" "$OLD_A"
run git shared publish
expect_out "autoPublish in config: si pubblica con publish" "sent to 1 of 1 peers"
git config --unset shared.autoPublish

# --- post-checkout: avviso solo sul branch attivo (REQ-F-013) -----------------
cd "$TRASH/A" || exit 1
git checkout -q -b feature 2>/dev/null
run git checkout -q main
expect_err "post-checkout: avviso sul branch attivo" \
	"breef.md: new version to apply (run 'git shared update')"
run git checkout -q feature
case "$ERR" in
*breef.md*) not_ok "post-checkout: nessun avviso sul branch sospeso" "$ERR" ;;
*) ok "post-checkout: nessun avviso sul branch sospeso" ;;
esac
git checkout -q main 2>/dev/null

run git shared update -c
expect_out "update -c: applicata e committata" "applied and committed"
expect_no_out "update -c: nessuna nuova versione" "new version created"

# --- blocchi di versioni precedenti: hook install li toglie -------------------
cd "$TRASH/B" || exit 1
OLD_BLOCK='# >>> git shared >>>
git-shared hook EVENT "$@" </dev/null || :
# <<< git shared <<<'
# old_hook <evento> [<righe dell'utente>]: hook con il blocco di una
# versione precedente subito dopo "#!/bin/sh"
old_hook() {
	printf '#!/bin/sh\n%s\n%s' "$(printf '%s' "$OLD_BLOCK" | sed "s/EVENT/$1/")" "$2" \
		>".git/hooks/$1"
	chmod +x ".git/hooks/$1"
}

# Il blocco rimasto nel post-commit non fa nulla al commit.
old_hook post-commit
printf 'terza riga di B\n' >>breef.md
OLD_A=$(git -C ../A rev-parse "refs/shared/$ID")
run git commit -q -a -m "B con il vecchio post-commit"
expect_eq "vecchio post-commit: commit senza output" "$OUT$ERR" ""
expect_eq "vecchio post-commit: A non riceve" \
	"$(git -C ../A rev-parse "refs/shared/$ID")" "$OLD_A"

run git shared hook install
expect_fail "post-commit creato da git shared: cancellato" test -e .git/hooks/post-commit

# post-commit dell'utente con il vecchio blocco: si toglie solo il blocco
old_hook post-commit 'echo "post-utente" >&2
'
git shared hook install >/dev/null
expect_eq "post-commit dell'utente: contenuto ripristinato" \
	"$(cat .git/hooks/post-commit)" "$(printf '#!/bin/sh\necho "post-utente" >&2')"

# pre-commit installato dalla prima versione di M3
old_hook pre-commit
git shared hook install >/dev/null
expect_fail "blocco pre-commit di M3 rimosso" test -e .git/hooks/pre-commit

# --- repo con il blocco ma senza .gitshared: nessun output ----------------------
make_repo E
cd "$TRASH/E" || exit 1
cp "$TRASH/A/.git/hooks/post-merge" .git/hooks/post-merge
put e.txt e
git checkout -q -b e2 2>/dev/null
put e.txt e2
git checkout -q main 2>/dev/null
run git merge -q e2
expect_eq "senza .gitshared: nessun output" "$OUT$ERR" ""

run git shared hook --help
expect_out "manuale: pagina degli hook" "post-checkout, post-merge, post-rewrite"
expect_no_out "manuale: niente shared.autoPublish" "autoPublish"

# --- publish non esegue il pre-push dell'utente (reportIdea C-20) ------------------
make_repo PA
cd "$TRASH/PA" || exit 1
put p.h v1
git shared set -c p.h >/dev/null
make_repo PB
cd "$TRASH/PB" || exit 1
put o.txt o
git shared link -c ../PA/p.h >/dev/null
cd "$TRASH/PA" || exit 1
printf '#!/bin/sh\necho RUN >>"%s/prepush.log"\nexit 1\n' "$TRASH" >.git/hooks/pre-push
chmod +x .git/hooks/pre-push
put p.h v2
run git shared publish
expect_rc "pre-push dell'utente: publish riuscito" 0
expect_out "pre-push dell'utente: inviata" "sent to 1 of 1 peers"
expect_fail "pre-push dell'utente: non eseguito" test -f "$TRASH/prepush.log"
run git -C ../PB shared status
expect_out "pre-push dell'utente: il peer la riceve" "new version to apply"

# --- modo degli hook: quello dell'utente non cambia -----------------------------
# Un hook non eseguibile e' un hook disattivato: inserire o togliere il
# blocco non lo riattiva. Solo l'hook creato da git shared e' eseguibile.
make_repo MO
cd "$TRASH/MO" || exit 1
put m.h m
printf '#!/bin/sh\necho "merge-utente" >&2\n' >.git/hooks/post-merge
chmod 644 .git/hooks/post-merge
printf '#!/bin/sh\necho "checkout-utente" >&2\n' >.git/hooks/post-checkout
chmod 700 .git/hooks/post-checkout
old_hook post-commit 'echo "post-utente" >&2'
chmod 600 .git/hooks/post-commit
git shared set -c m.h >/dev/null
expect_ok "modo: blocco nell'hook disattivato" has_block post-merge
expect_ok "modo: blocco nell'hook eseguibile" has_block post-checkout
expect_ok "modo: hook creato da git shared eseguibile" test -x .git/hooks/post-rewrite
expect_fail "modo: vecchio blocco tolto" has_block post-commit
if has_exec_bit; then
	expect_fail "modo: hook disattivato resta disattivato" test -x .git/hooks/post-merge
	expect_eq "modo: hook disattivato, permessi invariati" \
		"$(ls -l .git/hooks/post-merge | cut -c1-10)" "-rw-r--r--"
	expect_eq "modo: hook eseguibile, permessi invariati" \
		"$(ls -l .git/hooks/post-checkout | cut -c1-10)" "-rwx------"
	expect_eq "modo: blocco tolto, permessi invariati" \
		"$(ls -l .git/hooks/post-commit | cut -c1-10)" "-rw-------"
	git checkout -q -b mo2 2>/dev/null
	put n.txt n
	git checkout -q main 2>/dev/null
	run git -c advice.ignoredHook=false merge -q mo2
	expect_eq "modo: l'hook disattivato non gira" "$OUT$ERR" ""
else
	for t in "hook disattivato resta disattivato" "hook disattivato, permessi invariati" \
		"hook eseguibile, permessi invariati" "blocco tolto, permessi invariati" \
		"l'hook disattivato non gira"; do
		skip "modo: $t" "niente bit di esecuzione su questo filesystem"
	done
fi

done_testing
