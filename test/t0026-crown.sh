#!/bin/sh
# t0026-crown.sh - creatore e sovrani (D-111): crown per insediare e per
# seguire, crown --restore, cambi di autorita' ricevuti da accettare o
# rifiutare (crown --accept / --reject), unione a 3 vie della voce
# autorita', righe "creator" e "authority" di status, nota negli hook.
#
# Repo "remote" bare in $TRASH/srv, come in t0021:
#   A.git  il creatore (Luca)
#   C.git  il remote di Mario, che diventa sovrano
#   N.git  un posto nuovo, con una versione che qui non c'e'
# Repo locali: A (crea), B (peer locale di A), M (Mario, lettore di A),
# E (peer locale di M), R e S (lettori di A), Q (lettore che arriva dopo).

. "$(dirname "$0")/lib.sh"

# Gli eredi (D-114) li prova t0027: qui il proprio remote non si iscrive,
# cosi' le versioni restano quelle che il test si aspetta.
git config --global shared.heir false

tip() {
	git -C "$TRASH/$1" rev-parse -q --verify "refs/shared/${2:-$ID}"
}
srv_tip() {
	git --git-dir="$TRASH/srv/$1.git" rev-parse -q --verify "refs/shared/${2:-$ID}"
}
blob_of() { # <repo> <voce>: contenuto della voce nella T della repo
	git -C "$TRASH/$1" cat-file blob "refs/shared/$ID:$2" 2>/dev/null
}
key() { # <repo> <chiave>
	git -C "$TRASH/$1" config --get-all "shared.$ID.$2"
}
edit() {
	printf '%s\n' "$1" >>"${2:-p.h}"
	git add -- "${2:-p.h}"
	git commit -q -m "edit: $1" 2>/dev/null
}
# blocca / sblocca le scritture su un remote (un lettore senza permessi)
lock() {
	printf '#!/bin/sh\nexit 1\n' >"$TRASH/srv/$1.git/hooks/pre-receive"
	chmod +x "$TRASH/srv/$1.git/hooks/pre-receive"
}
unlock() {
	rm -f "$TRASH/srv/$1.git/hooks/pre-receive"
}

mkdir "$TRASH/srv"
for R in A C N; do
	git init -q --bare -b main "$TRASH/srv/$R.git"
done

# --- uso ---------------------------------------------------------------------------
make_repo A
cd "$TRASH/A" || exit 1
git remote add origin "$TRASH/srv/A.git"
printf 'l1\nl2\n' >p.h
git add p.h
git commit -q -m p.h
git shared set -c p.h >/dev/null
ID=$(shared_id p.h)
SRV_A=$(git remote get-url origin)

run git shared crown
expect_rc "uso: senza argomenti 129" 129
run git shared crown origin
expect_rc "un solo argomento: e' il path, l'URL vale origin (D-114)" 1
expect_err "un solo argomento: origin non e' un file shared" "origin"

run git shared crown --accept --reject p.h
expect_rc "uso: --accept e --reject insieme 129" 129
run git shared crown --restore origin p.h
expect_rc "uso: --restore con un URL 129" 129
run git shared crown --boh p.h
expect_rc "uso: opzione sconosciuta 129" 129
run git shared crown -h
expect_out "uso: -h" "usage: git shared crown"
run git shared crown --yes origin p.h
expect_rc "senza creatore: codice 1" 1
expect_err "senza creatore: lo dice" "never published to a remote"

# --- A crea; B peer locale; M, R, S lettori; E peer locale di M --------------------
git shared push origin p.h >/dev/null 2>&1
make_repo B
cd "$TRASH/B" || exit 1
put o.txt o
git shared link -c ../A/p.h >/dev/null 2>&1
for R in M R S; do
	make_repo "$R"
	cd "$TRASH/$R" || exit 1
	put o.txt o
	git shared link -c "$SRV_A" "$ID" >/dev/null 2>&1
done
cd "$TRASH/M" || exit 1
git remote add origin "$TRASH/srv/C.git"
SRV_C=$(git remote get-url origin)
make_repo E
cd "$TRASH/E" || exit 1
put o.txt o
git shared link -c ../M/p.h >/dev/null 2>&1
T0=$(tip A)
expect_eq "tutti partono dalla stessa versione" "$(tip M)$(tip E)$(tip R)" "$T0$T0$T0"

# --- conferma ----------------------------------------------------------------------
cd "$TRASH/M" || exit 1
run git shared crown origin p.h </dev/null
expect_rc "senza terminale e senza --yes: codice 1" 1
expect_err "senza terminale: dice --yes" "add --yes"
expect_out "l'avviso c'e' comunque" "the authority moves from $SRV_A"
expect_eq "senza conferma nulla cambia" "$(tip M)" "$T0"
expect_fail "senza conferma niente sul remote" srv_tip C

# --- insediare: Mario incorona il suo remote; A non accetta scritture -------------
lock A
run git shared crown --yes origin p.h
expect_rc "insediare: codice 0" 0
expect_out "insediare: l'avviso dice che servono i permessi" "becomes the sovereign of the file"
expect_out "insediare: l'avviso conta i peer locali" "1 local peers will have to accept it"
expect_out "insediare: fatto" "p.h: pushed to $SRV_C, now the sovereign of the file"
expect_err "insediare: i lettori di A non lo sapranno" "readers of $SRV_A will not be told"
T_M=$(tip M)
expect_eq "insediare: la versione e' sul sovrano" "$(srv_tip C)" "$T_M"
expect_eq "insediare: genitore T" "$(git rev-parse "$T_M^1")" "$T0"
expect_eq "insediare: stesso contenuto" "$(git rev-parse "$T_M:content")" \
	"$(git rev-parse "$T0:content")"
expect_eq "insediare: il creatore non cambia" "$(blob_of M creator)" "$SRV_A"
expect_eq "insediare: voce sovereigns" "$(blob_of M sovereigns)" "$SRV_C"
expect_eq "insediare: A non ha la versione" "$(srv_tip A)" "$T0"
run git shared status
expect_out "status: riga creator" "creator $SRV_A"
expect_no_out "status: la riga creator senza confronto" "creator $SRV_A ("
expect_out "status: riga authority con il confronto" "authority $SRV_C (same version, seen 20"
run git shared status --porcelain
expect_out "porcelain: riga creator" "creator $ID $SRV_A"
expect_out "porcelain: authority = sovrano, uguale, vista" "authority $ID same 1"
expect_out "porcelain: authority, URL del sovrano" " $SRV_C"
run git shared crown --yes origin p.h
expect_rc "di nuovo lo stesso: codice 0" 0
expect_out "di nuovo lo stesso: niente da fare" "is already the authority: nothing to do"

# --- il peer locale riceve il cambio e decide ------------------------------------
cd "$TRASH/E" || exit 1
run git shared status
expect_out "peer: riga authority ->" "authority -> $SRV_C, crowned by the local peer"
expect_out "peer: come decidere" "git shared crown --accept p.h"
run git shared status --porcelain
expect_out "peer, porcelain: auth-change da un peer" "auth-change $ID $T_M peer $SRV_C"
expect_out "peer, porcelain: auth-change-from" "auth-change-from $ID $T_M "
expect_no_out "peer, porcelain: nessun consiglio" "crown"
expect_eq "peer: T non avanza senza accettare" "$(tip E)" "$T0"
run git shared update
expect_err "peer: update avvisa" "authority change pending (-> $SRV_C)"
expect_eq "peer: update non avanza" "$(tip E)" "$T0"
git checkout -q -b altro 2>/dev/null
git checkout -q main 2>"$TRASH/.hook" # main e' il branch attivo del file
case "$(cat "$TRASH/.hook")" in
*"authority change pending"*) ok "hook: nota del cambio in attesa" ;;
*) not_ok "hook: nota del cambio in attesa" "stderr: $(cat "$TRASH/.hook")" ;;
esac
run git shared crown --accept p.h </dev/null
expect_rc "peer: --accept senza terminale codice 1" 1
expect_eq "peer: niente accettato" "$(key E authorityAccepted)" ""
run git shared crown --yes --accept p.h
expect_rc "peer: --accept codice 0" 0
expect_out "peer: accettata" "p.h: authority $SRV_C accepted"
expect_eq "peer: T avanza" "$(tip E)" "$T_M"
expect_eq "peer: authorityAccepted vale una volta" "$(key E authorityAccepted)" ""
run git shared status
expect_no_out "peer: niente piu' in attesa" "authority ->"
expect_out "peer: authority" "authority $SRV_C"
run git shared crown --accept p.h
expect_rc "peer: nulla da accettare codice 1" 1
expect_err "peer: lo dice" "no authority change to decide"

# --- il sovrano riceve: push va al sovrano ---------------------------------------
cd "$TRASH/M" || exit 1
edit m1
git shared publish >/dev/null 2>&1
run git shared push
expect_rc "push al sovrano: codice 0" 0
expect_out "push al sovrano: lo dice" "p.h: pushed to the sovereign $SRV_C"
expect_eq "push al sovrano: C avanza" "$(srv_tip C)" "$(tip M)"
expect_eq "push al sovrano: A no" "$(srv_tip A)" "$T0"
run git shared status --remote
expect_out "status --remote: autorita' allineata" "authority $SRV_C (same version"

# --- seguire senza permessi: R segue C -------------------------------------------
cd "$TRASH/R" || exit 1
lock C
run git shared crown --yes "$SRV_C" p.h
expect_rc "seguire: codice 0" 0
expect_out "seguire: l'avviso" "already governs the file: it is followed"
expect_out "seguire: fatto" "p.h: now follows $SRV_C"
expect_eq "seguire: T avanza in fast-forward" "$(tip R)" "$(srv_tip C)"
expect_eq "seguire: authorityAccepted tolta" "$(key R authorityAccepted)" ""
run git shared status
expect_out "seguire: status" "authority $SRV_C (same version"
unlock C

# --- seguire con versioni fatte qui: divergono -----------------------------------
cd "$TRASH/S" || exit 1
printf 's1\nl2\n' >p.h # la prima riga: M ha aggiunto in fondo
git commit -q -a -m s1 2>/dev/null
git shared publish >/dev/null 2>&1
T_S=$(tip S)
run git shared crown --yes "$SRV_C" p.h
expect_rc "seguire con modifiche: codice 0" 0
expect_out "seguire con modifiche: divergono, da unire" "must be merged"
run git shared update -c
expect_rc "seguire con modifiche: update codice 0" 0
expect_eq "seguire con modifiche: l'unione ha l'autorita' seguita" \
	"$(blob_of S sovereigns)" "$SRV_C"
expect_eq "seguire con modifiche: genitori T e la versione di C" \
	"$(git rev-parse "$(tip S)^1") $(git rev-parse "$(tip S)^2")" "$T_S $(srv_tip C)"

# --- ripristino di fabbrica ------------------------------------------------------
cd "$TRASH/M" || exit 1
mv "$TRASH/srv/A.git" "$TRASH/srv/A.off"
run git shared crown --yes --restore p.h
expect_rc "restore: creatore irraggiungibile codice 1" 1
expect_err "restore: lo dice" "cannot be reached or does not have the file"
expect_eq "restore: nulla cambia" "$(blob_of M sovereigns)" "$SRV_C"
mv "$TRASH/srv/A.off" "$TRASH/srv/A.git"
T_M=$(tip M)
run git shared crown --yes --restore p.h
expect_rc "restore: codice 0" 0
expect_out "restore: segue il creatore" "p.h: now follows $SRV_A"
expect_out "restore: le versioni del regno restano proposte" "'git shared push' proposes them"
expect_eq "restore: nuova versione sopra T" "$(git rev-parse "$(tip M)^1")" "$T_M"
expect_eq "restore: senza sovrani" "$(blob_of M sovereigns)" ""
expect_eq "restore: il creatore" "$(blob_of M creator)" "$SRV_A"
run git shared status
expect_out "restore: status, il confronto torna sul creatore" "creator $SRV_A (ahead"
expect_no_out "restore: nessuna riga authority" "authority "
run git shared crown --yes --restore p.h
expect_out "restore: gia' sulla linea del creatore" "already follows the creator's line"
run git shared push
expect_rc "restore: push rifiutato dal creatore chiuso, codice 1" 1
expect_err "restore: resta proposta" "not accepted by the creator $SRV_A"
unlock A

# --- passaggio di consegne: A insedia N, i suoi lettori lo ricevono ----------------
# N ha una versione che qui non c'e': il suo contenuto si perde, e l'avviso
# lo dice.
cd "$TRASH/B" || exit 1
X=$(git commit-tree -p "$T0" -m 'shared: p.h' "$(git rev-parse "$T0^{tree}")")
git push -q "$TRASH/srv/N.git" "$X:refs/shared/$ID" 2>/dev/null
cd "$TRASH/A" || exit 1
git remote add nuovo "$TRASH/srv/N.git"
SRV_N=$(git remote get-url nuovo)
run git shared crown --yes nuovo p.h
expect_rc "consegne: codice 0" 0
expect_out "consegne: versioni perse annunciate" "has 1 versions not here"
expect_out "consegne: sovrano" "now the sovereign of the file"
expect_out "consegne: anche sul vecchio" "pushed to $SRV_A too"
T_A=$(tip A)
expect_eq "consegne: N ha la versione" "$(srv_tip N)" "$T_A"
expect_eq "consegne: A ha la versione" "$(srv_tip A)" "$T_A"
expect_eq "consegne: secondo genitore la punta di N" "$(git rev-parse "$T_A^2")" "$X"

# Un lettore che arriva dopo segue l'autorita' scritta, senza accettare.
make_repo Q
cd "$TRASH/Q" || exit 1
put o.txt o
run git shared link -c "$SRV_A" "$ID"
expect_out "link da una copia: dice qual e' l'autorita'" \
	"$SRV_A is a copy: the authority of the file is $SRV_N"
run git shared status
expect_out "link dopo le consegne: segue l'autorita' scritta, senza accettare" "authority "
expect_no_out "link: niente in attesa" "authority ->"

# B, peer locale di A, riceve il cambio da A e lo rifiuta
cd "$TRASH/B" || exit 1
run git shared status
expect_out "B: cambio da un peer locale" "authority -> "
run git shared crown --reject p.h
expect_rc "B: --reject codice 0" 0
expect_out "B: rifiutato" "rejected: its versions are ignored"
expect_ok "B: authorityRejected" test -n "$(key B authorityRejected)"
# T si ferma all'ultima versione con l'autorita' di prima (B5): X, la
# versione di N che A ha incorporato.
expect_eq "B: T fino all'ultima versione con l'autorita' di prima" "$(tip B)" "$X"
run git shared status
expect_no_out "B: dopo il rifiuto niente riga" "authority ->"

# B resta all'autorita' vecchia e pubblica: A unisce senza conflitto
# sull'autorita' (l'ha cambiata un solo lato).
edit b1
git shared publish >/dev/null 2>&1
cd "$TRASH/A" || exit 1
run git shared update -c
expect_rc "3 vie: update codice 0" 0
expect_eq "3 vie: l'autorita' resta N" "$(blob_of A sovereigns | tail -n 1)" "$SRV_N"
expect_eq "3 vie: unione con la versione di B" "$(git rev-parse "$(tip A)^2")" "$(tip B)"
run git shared status
expect_no_out "3 vie: niente in attesa" "authority ->"

# B torna indietro: --accept riprende il cambio rifiutato
cd "$TRASH/B" || exit 1
run git shared crown --yes --accept "$SRV_N" p.h
expect_rc "B: --accept dopo il rifiuto codice 0" 0
expect_eq "B: authorityRejected tolta" "$(key B authorityRejected)" ""
run git shared update -c
expect_eq "B: ora l'autorita' e' N" "$(blob_of B sovereigns | tail -n 1)" "$SRV_N"

# --- l'autorita' stessa cambia: decide first --------------------------------------
# M segue A (crown --restore); A ha passato le consegne a N e lo ha scritto
# anche su A: pull lo riporta come cambio da decidere, e push lo dice
# invece di suggerire pull.
cd "$TRASH/M" || exit 1
run git shared pull
expect_err "consegne viste da M: pull avvisa" "authority change pending (-> $SRV_N)"
run git shared status
expect_out "consegne viste da M: dall'autorita' stessa" "authority -> $SRV_N, moved by the authority itself"
run git shared status --porcelain
expect_out "consegne, porcelain: dall'autorita'" " authority $SRV_N"
expect_no_out "consegne, porcelain: nessun from" "auth-change-from"
run git shared push
expect_rc "consegne viste da M: push codice 1" 1
expect_err "consegne viste da M: decide first" "authority change pending on $SRV_A: decide first"
run git shared crown --reject p.h
expect_rc "M rifiuta N: codice 0" 0

# --- voci non valide: la versione non e' candidata -------------------------------
# Sopra la punta di C (che dopo il --restore di M dichiara A: R ha quel
# cambio da decidere e lo rifiuta) una versione con un sovrano che
# diventerebbe un'opzione di git (D-107): non e' candidata.
cd "$TRASH/R" || exit 1
git shared pull >/dev/null 2>&1 # riceve la catena di C, che dichiara A
run git shared crown --reject p.h
T_R=$(tip R)
cg() { git --git-dir="$TRASH/srv/C.git" "$@"; }
P=$(cg rev-parse "refs/shared/$ID")
BAD=$(printf -- '--upload-pack=x\n' | cg hash-object -w --stdin)
TREE=$(printf '100644 blob %s\tcontent\n100644 blob %s\tcreator\n100644 blob %s\tsovereigns\n' \
	"$(cg rev-parse "$P:content")" "$(cg rev-parse "$P:creator")" "$BAD" |
	cg mktree)
V=$(cg commit-tree "$TREE" -p "$P" -m "shared: p.h")
cg update-ref "refs/shared/$ID" "$V"
run git shared pull
expect_eq "voci non valide: vista" "$(git rev-parse "refs/shared-authority/$ID")" "$V"
expect_eq "voci non valide: T non avanza" "$(tip R)" "$T_R"
run git shared status
expect_no_out "voci non valide: nessun cambio da decidere" "authority ->"

# Il sovrano non risponde: status --remote suggerisce, non cambia nulla.
mv "$TRASH/srv/C.git" "$TRASH/srv/C.off"
run git shared status --remote
expect_out "sovrano irraggiungibile: lo dice" "sovereign $SRV_C not reachable"
expect_out "sovrano irraggiungibile: suggerisce il ripristino" "'git shared crown --restore p.h'"
expect_eq "sovrano irraggiungibile: nulla cambia" "$(tip R)" "$T_R"
mv "$TRASH/srv/C.off" "$TRASH/srv/C.git"

cd "$TRASH/Q" || exit 1
run git shared crown --yes origin p.h
expect_rc "Q senza origin: URL sbagliato codice 1" 1
expect_err "Q: lo dice" "neither a remote of this repository nor a usable URL"

done_testing
