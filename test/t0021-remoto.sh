#!/bin/sh
# t0021-remoto.sh - prototipo del remoto: il creatore (D-103...D-107).
#
# Le repo "su GitHub" sono repo bare in $TRASH/srv: il trasporto e'
# quello di git, la differenza tra un URL e un path non conta. Lo scenario
# segue ideeRemote §4.6: A e B di Luca collegati in locale; A pubblica in
# remoto e srv/A.git diventa il creatore; C di Mario collega il file dal
# creatore; B pubblica (copia su srv/B.git, versione ufficiale sul
# creatore); Mario propone senza permessi; due editori in gara.

. "$(dirname "$0")/lib.sh"

# tip <repo>: refs/shared/<id> della repo
tip() {
	git -C "$TRASH/$1" rev-parse -q --verify "refs/shared/$ID"
}

# srv_tip <nome>: refs/shared/<id> della repo bare srv/<nome>.git
srv_tip() {
	git --git-dir="$TRASH/srv/$1.git" rev-parse -q --verify "refs/shared/$ID"
}

# creator_of <repo>: voce "creator" dell'ultima versione
creator_of() {
	git -C "$TRASH/$1" cat-file blob "refs/shared/$ID:creator" 2>/dev/null
}

# state_of: stato del primo file shared della repo corrente
state_of() {
	git shared status | sed -n 's/^    state  //p' | head -n 1
}

# edit <riga>: p.h (o il path dato) con una riga in piu', committato
edit() {
	printf '%s\n' "$1" >>"${2:-p.h}"
	git add -- "${2:-p.h}"
	git commit -q -m "edit: $1" 2>/dev/null
}

mkdir "$TRASH/srv"
for R in A B C E; do
	git init -q --bare -b main "$TRASH/srv/$R.git"
done

# --- A e B di Luca, collegati in locale come oggi ---------------------------------
make_repo A
cd "$TRASH/A" || exit 1
git remote add origin "$TRASH/srv/A.git"
printf 'l1\nl2\nl3\n' >p.h
git add p.h
git commit -q -m p.h
git shared set -c p.h >/dev/null
ID=$(shared_id p.h)
SRV_A=$(git remote get-url origin)

make_repo B
cd "$TRASH/B" || exit 1
git remote add origin "$TRASH/srv/B.git"
put o.txt o
git shared link -c ../A/p.h >/dev/null
SRV_B=$(git remote get-url origin)

# --- senza --remote e senza creatore: solo locale ---------------------------------
cd "$TRASH/A" || exit 1
run git shared publish
expect_rc "publish senza creatore: codice 0" 0
expect_eq "publish senza creatore: niente in remoto" "$(srv_tip A)" ""
expect_eq "publish senza creatore: nessuna voce creator" "$(creator_of A)" ""

run git shared publish --remote nessuno
expect_rc "publish --remote inesistente: codice 1" 1
expect_err "publish --remote inesistente: messaggio" "no remote named 'nessuno'"
run git shared publish --remote
expect_rc "publish --remote senza valore: uso" 129

# --- primo publish in remoto: srv/A.git diventa il creatore -----------------------
T0=$(tip A)
run git shared publish --remote origin
expect_rc "primo publish in remoto: codice 0" 0
expect_out "primo publish in remoto: messaggio" "which is now the creator of the file"
expect_out "primo publish in remoto: avviso a B" "p.h: 1 of 1 peers notified"
T1=$(tip A)
expect_eq "creatore: la ref e' su srv/A.git" "$(srv_tip A)" "$T1"
expect_eq "creatore: voce creator nella catena" "$(creator_of A)" "$SRV_A"
expect_eq "creatore: genitore la versione di prima" \
	"$(git rev-parse "$T1^")" "$T0"
expect_eq "creatore: stesso contenuto" \
	"$(git rev-parse "$T1:content")" "$(git rev-parse "$T0:content")"
expect_ok "creatore: avviso nel peer locale" test -f "$TRASH/B/.git/shared/inbox/$ID/$T1"

run git shared status
expect_out "status del creatore: riga creator" "    creator $SRV_A"
expect_out "status del creatore: allineato" "state  aligned"

# B riceve con l'avviso, come ogni versione: da qui conosce il creatore.
cd "$TRASH/B" || exit 1
run git shared status
expect_rc "B: status codice 0" 0
expect_out "B: riga creator" "    creator $SRV_A"
expect_eq "B: ha la versione del creatore" "$(tip B)" "$T1"

# --- --remote=<remote> su un file che ha gia' il creatore: niente di nuovo ---------
cd "$TRASH/A" || exit 1
run git shared publish --remote=origin
expect_rc "publish --remote=origin con creatore: codice 0" 0
expect_eq "publish --remote=origin: nessuna versione nuova" "$(tip A)" "$T1"
expect_out "publish dal creatore: il creatore ha gia' T" "the creator $SRV_A already has this version"
expect_no_out "publish dal creatore: niente copia su se stesso" "copied"

# --- link remoto: errori ----------------------------------------------------------
make_repo D
cd "$TRASH/D" || exit 1
put q.h q
git shared set -c q.h >/dev/null
ID_D=$(shared_id q.h)

make_repo C
cd "$TRASH/C" || exit 1
git remote add origin "$TRASH/srv/C.git"
put c.txt c
SRV_C=$(git remote get-url origin)

run git shared link ../D "$ID_D" q.h
expect_rc "link remoto senza creatore: codice 1" 1
expect_err "link remoto senza creatore: messaggio" "was never published to a remote"
expect_fail "link remoto senza creatore: nessuna ref temporanea" \
	git rev-parse -q --verify "refs/shared-incoming/$ID_D/0"
run git shared link "$SRV_A" 0123456789abcdef0123456789abcdef
expect_rc "link remoto, ID sconosciuto: codice 1" 1
expect_err "link remoto, ID sconosciuto: messaggio" "cannot get '0123456789abcdef0123456789abcdef'"
run git shared link "$TRASH/non-esiste.git" "$ID"
expect_rc "link remoto, repo inesistente: codice 1" 1
run git shared link "$SRV_A" "$ID" a b
expect_rc "link remoto, troppi argomenti: uso" 129
expect_fail "errori: niente .gitshared" test -f .gitshared

# --- Mario collega il file dal creatore -------------------------------------------
run git shared link -c "$SRV_A" "$ID" inc/p.h
expect_rc "link remoto: codice 0" 0
expect_out "link remoto: messaggio" "'inc/p.h' linked as 'p.h' from $SRV_A"
expect_ok "link remoto: contenuto del creatore" cmp -s inc/p.h ../A/p.h
expect_eq "link remoto: catena" "$(tip C)" "$T1"
expect_eq "link remoto: sezione in .gitshared" \
	"$(git config -f .gitshared shared.p.h.path)" "inc/p.h"
expect_fail "link remoto: nessun peer" git config --get-all "shared.$ID.peer"
expect_fail "link remoto: attesa chiusa" git config "shared.$ID.pending"
expect_fail "link remoto: nessuna chiave pendingRemote" git config "shared.$ID.pendingremote"
expect_eq "link remoto: il creatore non registra nulla" \
	"$(git -C "$TRASH/A" config --get-all "shared.$ID.peer" | wc -l | tr -d ' ')" "1"
run git shared status
expect_out "C: allineato" "state  aligned"
expect_out "C: riga creator" "    creator $SRV_A"

# Senza destinazione e senza -c: il nome del file, poi il commit.
make_repo C3
cd "$TRASH/C3" || exit 1
put c.txt c
run git shared link "$SRV_A" "$ID"
expect_rc "link remoto senza destinazione: codice 0" 0
expect_ok "link remoto senza destinazione: path = nome" test -f p.h
expect_eq "link remoto in attesa: pendingRemote" "$(git config "shared.$ID.pendingremote")" "true"
expect_eq "link remoto in attesa: stato" "$(state_of)" "pending commit"
git commit -q -m link 2>/dev/null
expect_fail "link remoto al commit: attesa chiusa" git config "shared.$ID.pending"
expect_eq "link remoto al commit: catena" "$(tip C3)" "$T1"
expect_eq "link remoto al commit: allineato" "$(state_of)" "aligned"

# --- copia sul proprio remote e clone ---------------------------------------------
cd "$TRASH/C" || exit 1
run git shared publish
expect_rc "C publish da allineato: codice 0" 0
expect_out "C publish: copia su srv/C.git" "p.h: versions copied to $SRV_C"
expect_out "C publish: niente push al creatore" "already has this version"
expect_eq "C publish: la catena e' su srv/C.git" "$(srv_tip C)" "$T1"
git push -q origin main 2>/dev/null

git clone -q "$TRASH/srv/C.git" "$TRASH/C2" 2>/dev/null
cd "$TRASH/C2" || exit 1
expect_eq "clone: scollegato" "$(state_of)" "unlinked"
run git shared link "$SRV_C" "$ID"
expect_rc "ri-aggancio dalla copia: codice 0" 0
expect_out "ri-aggancio dalla copia: subito" "'inc/p.h' linked as 'p.h' from $SRV_C"
run git shared status
expect_out "ri-aggancio: allineato" "state  aligned"
expect_out "ri-aggancio: il creatore resta srv/A.git" "    creator $SRV_A"

# --- B pubblica: copia su srv/B.git, versione ufficiale sul creatore ---------------
cd "$TRASH/B" || exit 1
edit b1
run git shared publish
expect_rc "B publish: codice 0" 0
expect_out "B publish: versione nuova" "p.h: new version created"
expect_out "B publish: avviso ad A" "p.h: 1 of 1 peers notified"
expect_out "B publish: copia" "p.h: versions copied to $SRV_B"
expect_out "B publish: creatore" "p.h: published to the creator $SRV_A"
T2=$(tip B)
expect_eq "B publish: srv/A.git ha la versione" "$(srv_tip A)" "$T2"
expect_eq "B publish: srv/B.git ha la versione" "$(srv_tip B)" "$T2"
expect_eq "B publish: la versione eredita il creatore" "$(creator_of B)" "$SRV_A"

# Mario riceve dal creatore: ls-remote, fetch, "da applicare".
cd "$TRASH/C" || exit 1
expect_eq "C: nuova versione dal creatore" "$(state_of)" "new version to apply"
expect_eq "C: T avanzata" "$(tip C)" "$T2"
expect_fail "C: nessuna ref temporanea rimasta" \
	git rev-parse -q --verify "refs/shared-incoming/$ID/creator"
run git shared update -c
expect_rc "C update: codice 0" 0
expect_ok "C update: contenuto di B" cmp -s inc/p.h ../B/p.h

cd "$TRASH/A" || exit 1
git shared update -c >/dev/null 2>&1
expect_eq "A: allineato dopo update" "$(state_of)" "aligned"

# --- Mario propone senza permessi: la versione resta da lui ------------------------
cat >"$TRASH/srv/A.git/hooks/pre-receive" <<'EOF'
#!/bin/sh
# solo per il test: chi ha GS_TEST_READONLY non ha i permessi di scrittura
test -z "$GS_TEST_READONLY"
EOF
chmod +x "$TRASH/srv/A.git/hooks/pre-receive"
cd "$TRASH/C" || exit 1
edit m1 inc/p.h
GS_TEST_READONLY=1
export GS_TEST_READONLY
run git shared publish
unset GS_TEST_READONLY
expect_rc "proposta: codice 1" 1
expect_err "proposta: non accettata dal creatore" "not accepted by the creator $SRV_A"
expect_out "proposta: copia su srv/C.git" "p.h: versions copied to $SRV_C"
T_PROP=$(tip C)
expect_eq "proposta: il creatore non cambia" "$(srv_tip A)" "$T2"
expect_eq "proposta: visibile su srv/C.git" "$(srv_tip C)" "$T_PROP"
expect_eq "proposta: per C resta allineato" "$(state_of)" "aligned"

# --- due editori in gara: il secondo push e' rifiutato, si unisce ------------------
cd "$TRASH/A" || exit 1
# in testa: B aggiunge in fondo, l'unione e' pulita
{ echo a1; cat p.h; } >p.tmp && mv p.tmp p.h
git commit -q -am "edit: a1" 2>/dev/null
run git shared publish
expect_rc "A publish: codice 0" 0
expect_out "A publish: sul creatore (e' la sua repo)" "p.h: published to the creator $SRV_A"
T3=$(tip A)
expect_eq "A publish: il creatore avanza" "$(srv_tip A)" "$T3"

# B non vede la versione di A: avviso perso e creatore che in lettura non
# risponde (insteadOf), mentre il push arriva (pushInsteadOf). E' la gara:
# il push di B parte da una versione vecchia.
cd "$TRASH/B" || exit 1
rm -rf ".git/shared/inbox/$ID"
git config "url.$TRASH/nessuno.git.insteadOf" "$SRV_A"
git config "url.$SRV_A.pushInsteadOf" "$SRV_A"
edit b2
run git shared publish
expect_rc "gara: codice 1" 1
expect_err "gara: creatore irraggiungibile in lettura" "creator $SRV_A not reachable"
expect_err "gara: il creatore ha una versione piu' nuova" "the creator $SRV_A has a newer version"
expect_eq "gara: il creatore non cambia" "$(srv_tip A)" "$T3"
git config --unset "url.$TRASH/nessuno.git.insteadOf"
git config --unset "url.$SRV_A.pushInsteadOf"

run git shared status
expect_out "gara: la versione del creatore va unita" "the version of $SRV_A must be merged"
run git shared update -c
expect_rc "gara: update unisce" 0
expect_out "gara: unione" "merged and committed"
run git shared publish
expect_rc "gara: publish dopo l'unione: codice 0" 0
expect_out "gara: publish dopo l'unione: creatore" "p.h: published to the creator $SRV_A"
T4=$(tip B)
expect_eq "gara: il creatore ha l'unione" "$(srv_tip A)" "$T4"
expect_eq "gara: l'unione ha due genitori" \
	"$(git rev-list --parents -n 1 "$T4" | wc -w | tr -d ' ')" "3"
expect_eq "gara: l'unione eredita il creatore" "$(creator_of B)" "$SRV_A"
expect_ok "gara: contenuto con entrambe le modifiche" grep -q a1 p.h
expect_ok "gara: contenuto con entrambe le modifiche (b2)" grep -q b2 p.h

# --- creatore irraggiungibile: una nota, lo stato dalle versioni locali ------------
mv "$TRASH/srv/A.git" "$TRASH/srv/A.via"
cd "$TRASH/C3" || exit 1
run git shared status
expect_rc "creatore sparito: status codice 0" 0
expect_out "creatore sparito: nota" "creator $SRV_A not reachable"
expect_out "creatore sparito: stato dalle versioni locali" "state  aligned"
run git shared update
expect_rc "creatore sparito: update codice 0" 0
expect_err "creatore sparito: avviso di update" "creator $SRV_A not reachable"
mv "$TRASH/srv/A.via" "$TRASH/srv/A.git"
expect_eq "creatore tornato: da applicare" "$(state_of)" "new version to apply"

# --- un creatore che sembra un'opzione non arriva mai a git ------------------------
make_repo F
cd "$TRASH/F" || exit 1
put f.txt f
BLOB=$(printf 'x\n' | git hash-object -w --stdin)
EVIL=$(printf -- '--upload-pack=touch %s/pwned\n' "$TRASH" | git hash-object -w --stdin)
TREE=$(printf '100644 blob %s\tcontent\n100644 blob %s\tcreator\n' "$BLOB" "$EVIL" | git mktree)
V=$(git commit-tree -m "shared: x.h" "$TREE")
ID_F=fedcba9876543210fedcba9876543210
git update-ref "refs/shared/$ID_F" "$V"
cd "$TRASH/C" || exit 1
run git shared link ../F "$ID_F" x.h
expect_rc "creatore come opzione: rifiutato" 1
expect_err "creatore come opzione: come senza creatore" "was never published to a remote"
expect_fail "creatore come opzione: nessun comando eseguito" test -e "$TRASH/pwned"

done_testing
