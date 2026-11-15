#!/bin/sh
# t0006-hooks.sh - hook: di default solo avvisi, pubblicazione al commit su
# richiesta (REQ-F-004, REQ-F-013, REQ-F-016, REQ-F-023, D-030, D-069,
# D-072, D-073).

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

# --- A: default (autoPublish spento) --------------------------------------
make_repo A
cd "$TRASH/A" || exit 1
printf '#!/bin/sh\necho "hook-utente" >&2\nexit 0\n' >.git/hooks/pre-commit
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

# --- B: link; di default il commit non pubblica (D-073) ----------------------
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

# --- autoPublish acceso: il post-commit pubblica -----------------------------
git config shared.autoPublish true
run git shared hook install
expect_rc "hook install: codice 0" 0
expect_out "hook install: pubblicazione al commit attiva" "Hooks installed (publish at commit: on)."
expect_ok "autoPublish: blocco nel post-commit" has_block post-commit
expect_fail "autoPublish: nessun pre-commit" has_block pre-commit
printf 'seconda riga di B\n' >>breef.md
run git commit -q -a -m "B modifica ancora"
expect_err "post-commit: nuova versione creata" "breef.md: new version created"
expect_err "post-commit: inviata ad A" "sent to 1 of 1 peers"
expect_eq "post-commit: A ha ricevuto" \
	"$(git -C ../A rev-parse "refs/shared/$ID")" "$(git rev-parse "refs/shared/$ID")"

# Commit che non tocca file shared: nessun output.
put note.txt n
printf 'n2\n' >note.txt
run git commit -q -a -m "solo note"
expect_eq "autoPublish: commit senza file shared, nessun output" "$OUT$ERR" ""

# --- A con autoPublish: commit da base vecchia -> avviso dopo il commit -------
cd "$TRASH/A" || exit 1
git config shared.autoPublish true
git shared hook install >/dev/null
OLD_HEAD=$(git rev-parse HEAD)
printf 'modifica di A da base vecchia\n' >>breef.md
run git commit -q -a -m "A da base vecchia"
expect_rc "base vecchia: il commit passa" 0
expect_err "base vecchia: avviso dopo il commit" "it is now diverged and it was not published"
expect_no_out "base vecchia: niente invio" "sent to"
git reset -q --hard "$OLD_HEAD"

# --- post-checkout: avviso solo sul branch attivo (REQ-F-013) -----------------
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

# update -c: il post-commit non crea una nuova versione per un update.
run git shared update -c
expect_out "update -c: applicata e committata" "applied and committed"
case "$OUT$ERR" in
*"new version created"*) not_ok "update -c: nessuna nuova versione" "$OUT$ERR" ;;
*) ok "update -c: nessuna nuova versione" ;;
esac

# --- autoPublish spento di nuovo: il post-commit si toglie ---------------------
cd "$TRASH/B" || exit 1
git config shared.autoPublish false
run git shared hook install
expect_out "hook install: pubblicazione al commit spenta" "(publish at commit: off)"
expect_fail "autoPublish spento: post-commit creato da git shared cancellato" \
	test -e .git/hooks/post-commit

# post-commit dell'utente: il blocco si aggiunge e si toglie senza toccare il resto
printf '#!/bin/sh\necho "post-utente" >&2\n' >.git/hooks/post-commit
git config shared.autoPublish true
git shared hook install >/dev/null
expect_ok "post-commit dell'utente: blocco aggiunto" has_block post-commit
git config shared.autoPublish false
git shared hook install >/dev/null
expect_eq "post-commit dell'utente: contenuto ripristinato" \
	"$(cat .git/hooks/post-commit)" "$(printf '#!/bin/sh\necho "post-utente" >&2')"

# pre-commit installato da M3: hook install lo ripulisce
printf '#!/bin/sh\n# >>> git shared >>>\ngit-shared hook pre-commit "$@" </dev/null || :\n# <<< git shared <<<\n' \
	>.git/hooks/pre-commit
git shared hook install >/dev/null
expect_fail "blocco pre-commit di M3 rimosso" test -e .git/hooks/pre-commit

# --- repo con il blocco ma senza .gitshared: nessun output ----------------------
make_repo E
cd "$TRASH/E" || exit 1
cp "$TRASH/A/.git/hooks/post-commit" .git/hooks/post-commit
put e.txt e
printf 'e2\n' >e.txt
run git commit -q -a -m e
expect_eq "senza .gitshared: nessun output" "$OUT$ERR" ""

run git shared help
expect_out "manuale: sezione HOOKS" "HOOKS"
expect_out "manuale: shared.autoPublish" "shared.autoPublish"

done_testing
