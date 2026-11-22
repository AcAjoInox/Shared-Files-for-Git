#!/bin/sh
# t0005-peers.sh - link, publish, update tra repo locali (M2).
# REQ-F-002, REQ-F-004..REQ-F-010, D-028, D-040, D-059.
#
# Il file condiviso e' test_shared_file.md (radice del progetto), copiato nella
# repo sorgente.

. "$(dirname "$0")/lib.sh"

TEST_FILE="test_shared_file.md"
SRC_FILE="$TEST_DIR/../$TEST_FILE"

# top <repo>: radice come la scrive git (su Windows "C:/...")
top() {
	git -C "$TRASH/$1" rev-parse --show-toplevel
}

# --- A: sorgente ---------------------------------------------------------------
make_repo A
cd "$TRASH/A" || exit 1
cp "$SRC_FILE" "$TEST_FILE"
git add "$TEST_FILE"
git commit -q -m init
git shared set -c "$TEST_FILE" >/dev/null
ID=$(shared_id "$TEST_FILE")
TOP_A=$(top A)

make_repo B
cd "$TRASH/B" || exit 1
put x.c x
TOP_B=$(top B)

# --- argomenti ----------------------------------------------------------------
run git shared link
expect_rc "link senza argomenti: codice 129" 129
run git shared link "../A/$TEST_FILE" a b
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
run git shared link "../A/$TEST_FILE"
expect_rc "link: codice 0" 0
expect_out "link: messaggio" "'$TEST_FILE' linked as '$TEST_FILE' from"
expect_ok "link: contenuto uguale alla sorgente" cmp -s "$TEST_FILE" "../A/$TEST_FILE"
expect_eq "link: .gitshared e file in staging" \
	"$(git diff --cached --name-only | tr '\n' ' ')" ".gitshared $TEST_FILE "
expect_eq "link: stesso ID della sorgente" "$(shared_id "$TEST_FILE")" "$ID"
expect_eq "link: attesa" "$(git config "shared.$ID.pending")" link
expect_eq "link: sorgente registrata" "$(git config "shared.$ID.pendingSource")" "$TOP_A"
expect_ok "link: catena ricevuta in una ref temporanea" \
	git rev-parse -q --verify "refs/shared-incoming/$ID/0"
expect_fail "link: refs/shared non ancora creata" git rev-parse -q --verify "refs/shared/$ID"
expect_fail "link: la sorgente non sa ancora nulla" git -C ../A config "shared.$ID.peer"
run git shared status
expect_out "link: in attesa di commit" "pending commit"
run git shared link "../A/$TEST_FILE"
expect_rc "link ripetuto: codice 1" 1

# --- commit: completamento, membership, annuncio ------------------------------
git commit -q -m "link $TEST_FILE"
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
run git shared link -c "../A/$TEST_FILE" "docs/shared copy.md"
expect_rc "link -c con destinazione: codice 0" 0
expect_out "link -c: confermato" "'docs/shared copy.md' linked as '$TEST_FILE' from"
expect_eq "link -c: path in .gitshared" \
	"$(git config -f .gitshared "shared.$TEST_FILE.path")" "docs/shared copy.md"
expect_eq "link -c: messaggio standard" "$(git log -1 --format=%s)" "Link docs/shared copy.md"
expect_eq "link -c: il commit contiene .gitshared e il file" \
	"$(git show --name-only --format= HEAD | tr '\n' ' ')" ".gitshared docs/shared copy.md "
PEERS_C=$(git config --get-all "shared.$ID.peer" | tr '\n' ' ')
case "$PEERS_C" in
*"$TOP_A"*"$TOP_B"* | *"$TOP_B"*"$TOP_A"*) ok "C conosce A e B (peer della sorgente)" ;;
*) not_ok "C conosce A e B (peer della sorgente)" "$PEERS_C" ;;
esac
expect_ok "A conosce C" sh -c "git -C '$TRASH/A' config --get-all shared.$ID.peer | grep -qx '$TOP_C'"
expect_ok "B conosce C" sh -c "git -C '$TRASH/B' config --get-all shared.$ID.peer | grep -qx '$TOP_C'"

make_repo D
cd "$TRASH/D" || exit 1
put "$TEST_FILE" altro
run git shared link "../A/$TEST_FILE"
expect_rc "destinazione esistente: codice 1" 1
expect_err "destinazione esistente: errore" "already exists"

# --- publish da B ---------------------------------------------------------------
cd "$TRASH/B" || exit 1
printf 'riga aggiunta da B\n' >>"$TEST_FILE"
git commit -q -a -m "B modifica $TEST_FILE"
run git shared status
expect_out "B: da pubblicare" "to publish"
run git shared publish
expect_rc "publish: codice 0" 0
expect_out "publish: nuova versione" "new version created"
expect_out "publish: inviata a tutti" "2 of 2 peers notified"
expect_eq "publish: due versioni" "$(git rev-list --count "refs/shared/$ID")" 2
T_B=$(git rev-parse "refs/shared/$ID")
expect_ok "publish: avviso in A" test -f "$TRASH/A/.git/shared/inbox/$ID/$T_B"
expect_ok "publish: avviso in C" test -f "$TRASH/C/.git/shared/inbox/$ID/$T_B"
expect_fail "publish: la ref di A non la scrive B" \
	test "$(git -C ../A rev-parse "refs/shared/$ID")" = "$T_B"
expect_eq "publish: B allineato" "$(git shared status | grep state)" "    state  aligned"
run git shared publish nessuno.md
expect_rc "publish di un file non shared: codice 1" 1

# --- update in A (ricevere non e' applicare) -------------------------------------
cd "$TRASH/A" || exit 1
expect_fail "A: file non toccato dalla ricezione" grep -q "riga aggiunta da B" "$TEST_FILE"
run git shared status
expect_out "A: da applicare" "new version to apply"
expect_eq "A: ricevuta da status" "$(git rev-parse "refs/shared/$ID")" "$T_B"
expect_fail "A: ricevere non e' applicare" grep -q "riga aggiunta da B" "$TEST_FILE"
run git shared update
expect_rc "update: codice 0" 0
expect_out "update: applicata, da committare" "new version applied. Commit it to confirm."
expect_ok "update: contenuto di B" grep -q "riga aggiunta da B" "$TEST_FILE"
expect_eq "update: file in staging, nessun commit" \
	"$(git diff --cached --name-only)" "$TEST_FILE"
git commit -q -m "aggiorno $TEST_FILE"
run git shared status
expect_out "A dopo il commit: allineato" "aligned"

cd "$TRASH/C" || exit 1
run git shared update -c
expect_rc "update -c: codice 0" 0
expect_out "update -c: applicata e committata" "new version applied and committed."
expect_no_out "update -c: nessun invito a committare" "Commit it to confirm"
expect_eq "update -c: messaggio standard" "$(git log -1 --format=%s)" "Update docs/shared copy.md"
run git shared status
expect_out "update -c: allineato" "aligned"
run git shared update
expect_out "update senza novita': aggiornato" "up to date"

# --- modifiche locali (REQ-F-010) --------------------------------------------------
cd "$TRASH/B" || exit 1
printf 'seconda riga di B\n' >>"$TEST_FILE"
git commit -q -a -m "B modifica ancora"
git shared publish >/dev/null
cd "$TRASH/A" || exit 1
printf 'modifica locale\n' >>"$TEST_FILE"
run git shared update
expect_rc "modifiche locali: codice 1" 1
expect_err "modifiche locali: rifiuto" "has local changes"
expect_ok "modifiche locali: file non toccato" grep -q "modifica locale" "$TEST_FILE"
run git shared update -f
expect_rc "update -f: codice 0" 0
expect_fail "update -f: modifica locale sovrascritta" grep -q "modifica locale" "$TEST_FILE"
git commit -q -a -m "aggiorno $TEST_FILE"

# --- divergenza locale: C modifica da una base vecchia --------------------------
cd "$TRASH/C" || exit 1
printf 'modifica di C\n' >>"docs/shared copy.md"
git commit -q -a -m "C modifica da base vecchia"
run git shared status
expect_out "C: divergente" "diverged"
run git shared publish
expect_rc "publish divergente: codice 1" 1
expect_err "publish divergente: niente invio" "diverged from the latest version"
run git shared update
expect_rc "update divergente: codice 1" 1
# C e B hanno aggiunto righe in fondo: conflitto (le unioni sono in t0009)
expect_err "update divergente: conflitto da risolvere" "merge conflict"

# --- peer irraggiungibile e avviso perso (D-099) ------------------------------------
cd "$TRASH" || exit 1
mv B B.off
cd "$TRASH/A" || exit 1
printf 'modifica di A\n' >>"$TEST_FILE"
git commit -q -a -m "A modifica"
run git shared publish
expect_rc "peer irraggiungibile: codice 0" 0
expect_out "peer irraggiungibile: inviata agli altri" "1 of 2 peers notified"
expect_err "peer irraggiungibile: avviso" "not reachable"
cd "$TRASH" || exit 1
mv B.off B
cd "$TRASH/B" || exit 1
printf 'modifica concorrente di B\n' >>"$TEST_FILE"
git commit -q -a -m "B modifica senza aver visto A"
run git shared publish
expect_rc "avviso perso: B pubblica" 0
expect_out "avviso perso: A e C notificati" "2 of 2 peers notified"
run git shared update
expect_out "avviso perso: senza --all B non sa di A" "up to date"
run git shared update --all
expect_rc "versioni divergenti tra peer: codice 1" 1
expect_err "versioni divergenti tra peer: conflitto da risolvere" "merge conflict"

# --- nomi con spazi e caratteri non ASCII (reportIdea C-01) ------------------
make_repo U1
cd "$TRASH/U1" || exit 1
mkdir "dir è"
put "dir è/perché.h" v1
git shared set -c "dir è/perché.h" >/dev/null
UID_=$(git config -f .gitshared 'shared.perché.h.id')

make_repo U2
cd "$TRASH/U2" || exit 1
put x.c x
run git shared link -c "../U1/dir è/perché.h"
expect_rc "link non ASCII: codice 0" 0
expect_out "link non ASCII: confermato" "'dir è/perché.h' linked as 'perché.h'"
expect_eq "link non ASCII: path in .gitshared" \
	"$(git config -f .gitshared 'shared.perché.h.path')" "dir è/perché.h"
expect_eq "link non ASCII: file scritto e committato" "$(git show 'HEAD:dir è/perché.h')" "v1"
expect_eq "link non ASCII: U1 conosce U2" \
	"$(git -C ../U1 config --get-all "shared.$UID_.peer")" "$(top U2)"
run git shared status
expect_out "link non ASCII: allineato" "aligned"
expect_eq "link non ASCII: nessun avviso" "$ERR" ""

# Destinazione non ASCII diversa dalla sorgente (D-028).
make_repo U3
cd "$TRASH/U3" || exit 1
put x.c x
run git shared link -c "../U1/dir è/perché.h" "inc/però.h"
expect_rc "destinazione non ASCII: codice 0" 0
expect_eq "destinazione non ASCII: file scritto" "$(cat inc/però.h)" "v1"
expect_eq "destinazione non ASCII: path in .gitshared" \
	"$(git config -f .gitshared 'shared.perché.h.path')" "inc/però.h"

# Ciclo completo: publish da U1, update in U2 e U3.
cd "$TRASH/U1" || exit 1
put "dir è/perché.h" v2
run git shared publish
expect_out "non ASCII: publish a entrambi" "2 of 2 peers notified"
cd "$TRASH/U2" || exit 1
run git shared update -c
expect_out "non ASCII: update in U2" "dir è/perché.h: new version applied and committed"
expect_eq "non ASCII: U2 ha v2" "$(git show 'HEAD:dir è/perché.h')" "v2"
cd "$TRASH/U3" || exit 1
git shared update -c >/dev/null 2>&1
expect_eq "non ASCII: U3 ha v2" "$(git show 'HEAD:inc/però.h')" "v2"

# --- link -c committa solo il file e .gitshared (B-25) ----------------------
# ab.h in staging resta fuori dal commit. Il nome "a[b].h" contiene un
# carattere jolly: con --literal-pathspecs (C-06) e' solo un nome.
make_repo G
cd "$TRASH/G" || exit 1
put ab.h a
printf 'in staging\n' >ab.h
git add ab.h
run git shared link -c ../U1/dir\ è/perché.h "a[b].h"
expect_rc "link -c con '[': codice 0" 0
expect_eq "link -c: il commit contiene solo file e .gitshared" \
	"$(git show --name-only --format= HEAD | sort | tr '\n' ' ')" ".gitshared a[b].h "
expect_eq "link -c: ab.h resta in staging" "$(git diff --cached --name-only)" "ab.h"

done_testing
