#!/bin/sh
# t0005-peers.sh - link, publish, update tra repo locali (M2).
# REQ-F-002, REQ-F-004..REQ-F-010, D-028, D-040, D-059.
#
# Il file condiviso e' breef.md del progetto, copiato nella repo sorgente.

. "$(dirname "$0")/lib.sh"

BREEF="$TEST_DIR/../breef.md"

# top <repo>: radice come la scrive git (su Windows "C:/...")
top() {
	git -C "$TRASH/$1" rev-parse --show-toplevel
}

# --- A: sorgente ---------------------------------------------------------------
make_repo A
cd "$TRASH/A" || exit 1
cp "$BREEF" breef.md
git add breef.md
git commit -q -m init
git shared set -c breef.md >/dev/null
ID=$(shared_id breef.md)
TOP_A=$(top A)

make_repo B
cd "$TRASH/B" || exit 1
put x.c x
TOP_B=$(top B)

# --- argomenti ----------------------------------------------------------------
run git shared link
expect_rc "link senza argomenti: codice 129" 129
run git shared link ../A/breef.md a b
expect_rc "link con tre argomenti: codice 129" 129
run git shared link ../A/x.md
expect_rc "sorgente non shared: codice 1" 1
expect_err "sorgente non shared: errore" "is not shared in its repository"
run git shared link "$TRASH/nessuna/cartella/f.md"
expect_rc "sorgente fuori da una repo: codice 1" 1
expect_err "sorgente fuori da una repo: errore" "not inside a git repository"
run git shared link x.c
expect_rc "stessa repo: codice 1" 1
expect_err "stessa repo: errore" "same repository"

# --- link senza commit: tutto in attesa ---------------------------------------
run git shared link ../A/breef.md
expect_rc "link: codice 0" 0
expect_out "link: messaggio" "'breef.md' linked as 'breef.md' from"
expect_ok "link: contenuto uguale alla sorgente" cmp -s breef.md ../A/breef.md
expect_eq "link: .gitshared e file in staging" \
	"$(git diff --cached --name-only | tr '\n' ' ')" ".gitshared breef.md "
expect_eq "link: stesso ID della sorgente" "$(shared_id breef.md)" "$ID"
expect_eq "link: attesa" "$(git config "shared.$ID.pending")" link
expect_eq "link: sorgente registrata" "$(git config "shared.$ID.pendingSource")" "$TOP_A"
expect_ok "link: catena ricevuta in una ref temporanea" \
	git rev-parse -q --verify "refs/shared-incoming/$ID/0"
expect_fail "link: refs/shared non ancora creata" git rev-parse -q --verify "refs/shared/$ID"
expect_fail "link: la sorgente non sa ancora nulla" git -C ../A config "shared.$ID.peer"
run git shared status
expect_out "link: in attesa di commit" "pending commit"
run git shared link ../A/breef.md
expect_rc "link ripetuto: codice 1" 1

# --- commit: completamento, membership, annuncio ------------------------------
git commit -q -m "link breef.md"
run git shared status
expect_out "dopo il commit: allineato" "aligned"
expect_eq "catena uguale alla sorgente" \
	"$(git rev-parse "refs/shared/$ID")" "$(git -C ../A rev-parse "refs/shared/$ID")"
expect_fail "ref temporanea cancellata" git rev-parse -q --verify "refs/shared-incoming/$ID/0"
expect_eq "B conosce A" "$(git config --get-all "shared.$ID.peer")" "$TOP_A"
expect_eq "A conosce B" "$(git -C ../A config --get-all "shared.$ID.peer")" "$TOP_B"
expect_fail "attesa rimossa" git config "shared.$ID.pending"
expect_fail "sorgente dell'attesa rimossa" git config "shared.$ID.pendingSource"

# --- link con destinazione diversa e -c: C conosce A e B -----------------------
make_repo C
cd "$TRASH/C" || exit 1
put c.c c
TOP_C=$(top C)
run git shared link -c ../A/breef.md "docs/breef copy.md"
expect_rc "link -c con destinazione: codice 0" 0
expect_out "link -c: confermato" "'docs/breef copy.md' linked as 'breef.md' from"
expect_eq "link -c: path in .gitshared" \
	"$(git config -f .gitshared shared.breef.md.path)" "docs/breef copy.md"
expect_eq "link -c: messaggio standard" "$(git log -1 --format=%s)" "Link docs/breef copy.md"
expect_eq "link -c: il commit contiene .gitshared e il file" \
	"$(git show --name-only --format= HEAD | tr '\n' ' ')" ".gitshared docs/breef copy.md "
PEERS_C=$(git config --get-all "shared.$ID.peer" | tr '\n' ' ')
case "$PEERS_C" in
*"$TOP_A"*"$TOP_B"* | *"$TOP_B"*"$TOP_A"*) ok "C conosce A e B (peer della sorgente)" ;;
*) not_ok "C conosce A e B (peer della sorgente)" "$PEERS_C" ;;
esac
expect_ok "A conosce C" sh -c "git -C '$TRASH/A' config --get-all shared.$ID.peer | grep -qx '$TOP_C'"
expect_ok "B conosce C" sh -c "git -C '$TRASH/B' config --get-all shared.$ID.peer | grep -qx '$TOP_C'"

make_repo D
cd "$TRASH/D" || exit 1
put breef.md altro
run git shared link ../A/breef.md
expect_rc "destinazione esistente: codice 1" 1
expect_err "destinazione esistente: errore" "already exists"

# --- publish da B ---------------------------------------------------------------
cd "$TRASH/B" || exit 1
printf 'riga aggiunta da B\n' >>breef.md
git commit -q -a -m "B modifica breef.md"
run git shared status
expect_out "B: da pubblicare" "to publish"
run git shared publish
expect_rc "publish: codice 0" 0
expect_out "publish: nuova versione" "new version created"
expect_out "publish: inviata a tutti" "sent to 2 of 2 peers"
expect_eq "publish: due versioni" "$(git rev-list --count "refs/shared/$ID")" 2
expect_eq "publish: A ha ricevuto" \
	"$(git -C ../A rev-parse "refs/shared/$ID")" "$(git rev-parse "refs/shared/$ID")"
expect_eq "publish: C ha ricevuto" \
	"$(git -C ../C rev-parse "refs/shared/$ID")" "$(git rev-parse "refs/shared/$ID")"
expect_eq "publish: B allineato" "$(git shared status | grep state)" "    state  aligned"
run git shared publish nessuno.md
expect_rc "publish di un file non shared: codice 1" 1

# --- update in A (ricevere non e' applicare) -------------------------------------
cd "$TRASH/A" || exit 1
expect_fail "A: file non toccato dalla ricezione" grep -q "riga aggiunta da B" breef.md
run git shared status
expect_out "A: da applicare" "new version to apply"
run git shared update
expect_rc "update: codice 0" 0
expect_out "update: applicata" "new version applied"
expect_ok "update: contenuto di B" grep -q "riga aggiunta da B" breef.md
expect_eq "update: file in staging, nessun commit" \
	"$(git diff --cached --name-only)" breef.md
git commit -q -m "aggiorno breef.md"
run git shared status
expect_out "A dopo il commit: allineato" "aligned"

cd "$TRASH/C" || exit 1
run git shared update -c
expect_rc "update -c: codice 0" 0
expect_eq "update -c: messaggio standard" "$(git log -1 --format=%s)" "Update docs/breef copy.md"
run git shared status
expect_out "update -c: allineato" "aligned"
run git shared update
expect_out "update senza novita': aggiornato" "up to date"

# --- modifiche locali (REQ-F-010) --------------------------------------------------
cd "$TRASH/B" || exit 1
printf 'seconda riga di B\n' >>breef.md
git commit -q -a -m "B modifica ancora"
git shared publish >/dev/null
cd "$TRASH/A" || exit 1
printf 'modifica locale\n' >>breef.md
run git shared update
expect_rc "modifiche locali: codice 1" 1
expect_err "modifiche locali: rifiuto" "has local changes"
expect_ok "modifiche locali: file non toccato" grep -q "modifica locale" breef.md
run git shared update -f
expect_rc "update -f: codice 0" 0
expect_fail "update -f: modifica locale sovrascritta" grep -q "modifica locale" breef.md
git commit -q -a -m "aggiorno breef.md"

# --- divergenza locale: C modifica da una base vecchia --------------------------
cd "$TRASH/C" || exit 1
printf 'modifica di C\n' >>"docs/breef copy.md"
git commit -q -a -m "C modifica da base vecchia"
run git shared status
expect_out "C: divergente" "diverged"
run git shared publish
expect_rc "publish divergente: codice 1" 1
expect_err "publish divergente: niente invio" "diverged from the latest version"
run git shared update
expect_rc "update divergente: codice 1" 1
expect_err "update divergente: merge non ancora disponibile" "merging is not implemented yet"

# --- peer irraggiungibile e push rifiutato ----------------------------------------
cd "$TRASH" || exit 1
mv B B.off
cd "$TRASH/A" || exit 1
printf 'modifica di A\n' >>breef.md
git commit -q -a -m "A modifica"
run git shared publish
expect_rc "peer irraggiungibile: codice 0" 0
expect_out "peer irraggiungibile: inviata agli altri" "sent to 1 of 2 peers"
expect_err "peer irraggiungibile: avviso" "not reachable"
cd "$TRASH" || exit 1
mv B.off B
cd "$TRASH/B" || exit 1
printf 'modifica concorrente di B\n' >>breef.md
git commit -q -a -m "B modifica senza aver visto A"
run git shared publish
expect_rc "push rifiutato: codice 1" 1
expect_err "push rifiutato: errore" "rejected by"
run git shared update
expect_rc "versioni divergenti tra peer: codice 1" 1
expect_err "versioni divergenti tra peer: segnalate" "merging is not implemented yet"

done_testing
