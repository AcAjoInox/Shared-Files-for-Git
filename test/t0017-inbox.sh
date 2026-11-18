#!/bin/sh
# t0017-inbox.sh - notifica invece del push (D-099, A-31).
# REQ-F-004, REQ-F-007, REQ-F-008, REQ-F-009, REQ-F-012, REQ-F-014.
#
# publish scrive in ogni peer un avviso, .git/shared/inbox/<id>/<oid>; il
# peer riceve con il proprio fetch al primo comando che ricava lo stato.

. "$(dirname "$0")/lib.sh"

top() {
	git -C "$TRASH/$1" rev-parse --show-toplevel
}

# tip <repo>: refs/shared/<id> della repo
tip() {
	git -C "$TRASH/$1" rev-parse -q --verify "refs/shared/$ID"
}

# inbox <repo>: cartella degli avvisi dell'ID nella repo
inbox() {
	echo "$TRASH/$1/.git/shared/inbox/$ID"
}

# notices <repo>: quanti avvisi ci sono
notices() {
	ls "$(inbox "$1")" 2>/dev/null | wc -l | tr -d ' '
}

# edit <riga 1> <riga 3>: p.h di tre righe, committato
edit() {
	printf '%s\nl2\n%s\n' "$1" "$2" >p.h
	git add p.h
	git commit -q -m "p.h: $1 $2"
}

make_repo A
cd "$TRASH/A" || exit 1
edit l1 l3
git shared set -c p.h >/dev/null
ID=$(shared_id p.h)
for R in B C; do
	make_repo "$R"
	cd "$TRASH/$R" || exit 1
	put o.txt o
	git shared link -c ../A/p.h >/dev/null
done
TOP_A=$(top A)
TOP_B=$(top B)
TOP_C=$(top C)

# --- invio: un avviso per peer, nessuna ref scritta nei peer ---------------------------
cd "$TRASH/A" || exit 1
OLD=$(tip B)
edit a1 l3
run git shared publish
expect_rc "publish: codice 0" 0
expect_out "publish: peer notificati" "p.h: 2 of 2 peers notified"
T1=$(tip A)
expect_ok "publish: avviso in B" test -f "$(inbox B)/$T1"
expect_ok "publish: avviso in C" test -f "$(inbox C)/$T1"
expect_eq "publish: l'avviso dice chi pubblica" "$(cat "$(inbox B)/$T1")" "$TOP_A"
expect_eq "publish: la ref di B non cambia" "$(tip B)" "$OLD"
expect_eq "publish: nessun file temporaneo" "$(ls "$(inbox B)" | tr '\n' ' ')" "$T1 "
run git shared publish
expect_out "publish ripetuto: avviso gia' scritto" "p.h: 2 of 2 peers notified"
expect_eq "publish ripetuto: un solo avviso" "$(notices B)" 1
run git shared status --peers
expect_out "status --peers: annunciata, non ricevuta" "$TOP_B: announced, not received yet: nothing to do"

# --- ricezione da status ---------------------------------------------------------------
cd "$TRASH/B" || exit 1
run git shared status
expect_rc "status riceve: codice 0" 0
expect_out "status riceve: da applicare" "new version to apply"
expect_eq "status riceve: T avanzata" "$(tip B)" "$T1"
expect_eq "status riceve: avviso cancellato" "$(notices B)" 0
expect_no_out "status riceve: nessuna nota" "note"
expect_eq "status riceve: file non toccato" "$(head -1 p.h)" "l1"
git shared update -c >/dev/null

# --- ricezione da update ----------------------------------------------------------------
cd "$TRASH/C" || exit 1
run git shared update -c
expect_rc "update riceve: codice 0" 0
expect_out "update riceve: ricevuta" "p.h: new version received"
expect_out "update riceve: applicata" "p.h: new version applied and committed."
expect_eq "update riceve: avviso cancellato" "$(notices C)" 0

# --- update senza avvisi: nessun fetch; --all: da tutti --------------------------------
mv "$TRASH/A" "$TRASH/A.off"
run git shared update
expect_rc "senza avvisi: codice 0" 0
expect_out "senza avvisi: aggiornato" "p.h: up to date"
expect_eq "senza avvisi: nessun fetch, nessun avviso di peer" "$ERR" ""
run git shared update --all
expect_rc "--all: codice 0" 0
expect_err "--all: fetch da tutti i peer" "peer $TOP_A not reachable"
mv "$TRASH/A.off" "$TRASH/A"
run git shared update --abort --all
expect_rc "--abort con --all: codice 129" 129
run git shared update -h
expect_out "uso: --all" "--all             fetch from every peer"

# --- ricezione da publish: la versione nuova cambia lo stato -------------------------
cd "$TRASH/A" || exit 1
edit a2 l3
git shared publish >/dev/null
T2=$(tip A)
cd "$TRASH/C" || exit 1
printf 'a1\nl2\nc3\n' >p.h
git commit -q -a -m "C modifica senza aver ricevuto"
run git shared publish
expect_rc "publish riceve: codice 1" 1
expect_err "publish riceve: ora e' divergente" "diverged from the latest version"
expect_eq "publish riceve: T avanzata" "$(tip C)" "$T2"
git shared update -c >/dev/null 2>&1
git shared publish >/dev/null 2>&1
cd "$TRASH/A" && git shared update -c >/dev/null 2>&1
cd "$TRASH/B" && git shared update -c >/dev/null 2>&1

# --- avviso perso: lo riscrive il publish successivo, o update --all ----------------
cd "$TRASH/A" || exit 1
mv "$TRASH/B" "$TRASH/B.off"
edit a3 c3
run git shared publish
expect_out "B spostata: notificato solo C" "p.h: 1 of 2 peers notified"
expect_err "B spostata: irraggiungibile" "peer $TOP_B not reachable"
mv "$TRASH/B.off" "$TRASH/B"
cd "$TRASH/B" || exit 1
run git shared status
expect_out "avviso perso: B si crede allineata" "aligned"
cd "$TRASH/A" || exit 1
run git shared publish
expect_out "publish da allineato: riscrive l'avviso" "p.h: 2 of 2 peers notified"
cd "$TRASH/B" || exit 1
run git shared status
expect_out "avviso riscritto: da applicare" "new version to apply"
git shared update -c >/dev/null

cd "$TRASH/A" || exit 1
mv "$TRASH/B" "$TRASH/B.off"
edit a4 c3
git shared publish >/dev/null 2>&1
T4=$(tip A)
mv "$TRASH/B.off" "$TRASH/B"
cd "$TRASH/B" || exit 1
run git shared update -c
expect_out "avviso perso, update senza --all: nulla" "p.h: up to date"
run git shared update --all -c
expect_out "avviso perso, update --all: ricevuta" "p.h: new version received"
expect_eq "avviso perso, update --all: T di A" "$(tip B)" "$T4"
cd "$TRASH/C" && git shared update -c >/dev/null 2>&1

# --- fetch fallito: l'avviso resta e status lo dice -----------------------------------
cd "$TRASH/A" || exit 1
edit a5 c3
git shared publish >/dev/null
T5=$(tip A)
mv "$TRASH/A" "$TRASH/A.off"
cd "$TRASH/B" || exit 1
run git shared status
expect_rc "fetch fallito: codice 0" 0
expect_out "fetch fallito: stato da T" "aligned"
expect_out "fetch fallito: nota" "note   new version announced by $TOP_A, not received: run 'git shared update'"
expect_err "fetch fallito: avviso del peer" "peer $TOP_A not reachable"
expect_eq "fetch fallito: l'avviso resta" "$(notices B)" 1
mv "$TRASH/A.off" "$TRASH/A"
run git shared status
expect_out "fetch riuscito: da applicare" "new version to apply"
expect_eq "fetch riuscito: avviso cancellato" "$(notices B)" 0
git shared update -c >/dev/null
cd "$TRASH/C" && git shared update -c >/dev/null 2>&1

# --- avviso divergente: da unire, senza un secondo fetch -----------------------------
# C pubblica mentre B e' spostata (B non lo sa); poi B pubblica da T5.
cd "$TRASH/C" || exit 1
mv "$TRASH/B" "$TRASH/B.off"
edit a5 c6
git shared publish >/dev/null 2>&1
mv "$TRASH/B.off" "$TRASH/B"
cd "$TRASH/B" || exit 1
edit b6 c3
git shared publish >/dev/null 2>&1
TB=$(tip B)
cd "$TRASH/C" || exit 1
run git shared status
expect_out "divergente: C allineata alla sua T" "aligned"
expect_out "divergente: nota da unire" "note   the version of $TOP_B must be merged: run 'git shared update'"
expect_eq "divergente: l'avviso resta" "$(notices C)" 1
mv "$TRASH/B" "$TRASH/B.off"
run git shared status
expect_out "divergente, B spostata: nota ancora" "the version of $TOP_B must be merged"
expect_eq "divergente: nessun secondo fetch" "$ERR" ""
mv "$TRASH/B.off" "$TRASH/B"
run git shared publish
expect_rc "divergente: publish rifiutato" 1
expect_err "divergente: prima l'unione" "does not follow this one"
run git shared update -c
expect_rc "divergente: update unisce" 0
expect_out "divergente: unita" "merged and committed"
expect_eq "divergente: contenuto unito" "$(cat p.h | tr '\n' ' ')" "b6 l2 c6 "
run git shared publish
expect_rc "dopo l'unione: publish" 0
expect_eq "dopo l'unione: avviso cancellato" "$(notices C)" 0
cd "$TRASH/B" || exit 1
run git shared update -c
expect_out "B riceve l'unione" "new version applied and committed."
expect_eq "B: contenuto unito" "$(cat p.h | tr '\n' ' ')" "b6 l2 c6 "
cd "$TRASH/A" && git shared update -c >/dev/null 2>&1

# --- repo annunciante sconosciuta: fetch da tutti i peer -------------------------------
cd "$TRASH/A" || exit 1
mv "$TRASH/B" "$TRASH/B.off"
edit a7 c6
git shared publish >/dev/null 2>&1
T7=$(tip A)
mv "$TRASH/B.off" "$TRASH/B"
mkdir -p "$(inbox B)"
printf '/non/esiste/A\n' >"$(inbox B)/$T7"
cd "$TRASH/B" || exit 1
run git shared status
expect_out "repo sconosciuta: ricevuta dai peer" "new version to apply"
expect_eq "repo sconosciuta: T di A" "$(tip B)" "$T7"
expect_eq "repo sconosciuta: avviso cancellato" "$(notices B)" 0
git shared update -c >/dev/null

# --- nomi non validi e ID sconosciuti: ignorati, prune li toglie -----------------------
printf 'x\n' >"$(inbox B)/non-un-oid"
mkdir -p "$TRASH/B/.git/shared/inbox/0123456789abcdef0123456789abcdef"
printf '%s\n' "$TOP_A" >"$TRASH/B/.git/shared/inbox/0123456789abcdef0123456789abcdef/$T7"
run git shared status
expect_rc "avvisi non validi: codice 0" 0
expect_out "avvisi non validi: allineata" "aligned"
expect_eq "avvisi non validi: nessun errore" "$ERR" ""
run git shared prune -n
expect_out "prune -n: avvisi di un ID sconosciuto" \
	"would remove leftover .git/shared/inbox/0123456789abcdef0123456789abcdef"
expect_ok "prune -n: non tocca nulla" test -d "$TRASH/B/.git/shared/inbox/0123456789abcdef0123456789abcdef"
run git shared prune
expect_out "prune: avvisi di un ID sconosciuto" \
	"removed leftover .git/shared/inbox/0123456789abcdef0123456789abcdef"
expect_fail "prune: cartella tolta" test -d "$TRASH/B/.git/shared/inbox/0123456789abcdef0123456789abcdef"
expect_ok "prune: la cartella di un ID collegato resta" test -d "$(inbox B)"
rm -f "$(inbox B)/non-un-oid"

# --- hook: avviso dopo un checkout, senza fetch -----------------------------------------
# Il post-checkout verso main, dove p.h e' attivo.
cd "$TRASH/B" || exit 1
git checkout -q -b altro 2>/dev/null
cd "$TRASH/A" || exit 1
edit a8 c6
git shared publish >/dev/null 2>&1
cd "$TRASH/B" || exit 1
OLD=$(tip B)
run git checkout -q main
expect_err "hook: versione annunciata" "p.h: new version announced by a peer (run 'git shared update')"
expect_eq "hook: nessuna ricezione" "$(tip B)" "$OLD"
expect_eq "hook: l'avviso resta" "$(notices B)" 1
git shared update -c >/dev/null
cd "$TRASH/C" && git shared update -c >/dev/null 2>&1

# --- unlink: gli avvisi della repo spariscono ------------------------------------------
cd "$TRASH/A" || exit 1
edit a9 c6
git shared publish >/dev/null 2>&1
cd "$TRASH/C" || exit 1
git shared unlink -c p.h >/dev/null 2>&1
expect_fail "unlink: avvisi tolti" test -d "$(inbox C)"

# --- ripiego sul push: peer con .git file ---------------------------------------------
git init -q -b main --separate-git-dir "$TRASH/H.git" "$TRASH/H"
cd "$TRASH/H" || exit 1
put o.txt o
run git shared link -c ../A/p.h
expect_rc "peer con .git file: link" 0
cd "$TRASH/A" || exit 1
edit a10 c6
run git shared publish
expect_rc "ripiego: codice 0" 0
expect_out "ripiego: tutti raggiunti" "p.h: 2 of 2 peers notified"
expect_eq "ripiego: H ha ricevuto con il push" "$(tip H)" "$(tip A)"
expect_ok "ripiego: B ha l'avviso" test -f "$(inbox B)/$(tip A)"

done_testing
