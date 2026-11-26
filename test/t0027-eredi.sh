#!/bin/sh
# t0027-eredi.sh - gli eredi, la linea di successione (D-114): iscrizione
# al push, casata, shared.heir, lettore senza permessi, riga "heirs" di
# status, visita degli eredi quando l'autorita' non risponde (pull segue da
# solo un erede incoronato), cartello stradale, ritorno del creatore,
# autorita' che non e' un erede, crown senza URL.
#
# Repo "remote" bare in $TRASH/srv/<casata>/<nome>.git:
#   luca/A   il creatore (Luca)         luca/B, luca/L  altri progetti di Luca
#   anna/D   Anna, diventa sovrana      paolo/F, mario/C  altri editori
#   marco/G  erede di Anna              nessuno/N  un sovrano che non e' erede
# Repo locali con lo stesso nome; R e S sono lettori senza remote.

. "$(dirname "$0")/lib.sh"

tip() {
	git -C "$TRASH/$1" rev-parse -q --verify "refs/shared/$ID"
}
srv() { # <casata/nome>: URL della repo remota
	printf '%s' "$SRVROOT/$1.git"
}
srv_tip() {
	git --git-dir="$TRASH/srv/$1.git" rev-parse -q --verify "refs/shared/$ID"
}
srv_heirs() { # <casata/nome>: la voce heirs sulla repo remota
	git --git-dir="$TRASH/srv/$1.git" cat-file blob "refs/shared/$ID:heirs" 2>/dev/null
}
edit() {
	printf '%s\n' "$1" >>p.h
	git add -- p.h
	git commit -q -m "edit: $1" 2>/dev/null
}
lock() {
	printf '#!/bin/sh\nexit 1\n' >"$TRASH/srv/$1.git/hooks/pre-receive"
	chmod +x "$TRASH/srv/$1.git/hooks/pre-receive"
}
unlock() {
	rm -f "$TRASH/srv/$1.git/hooks/pre-receive"
}
kill_srv() { # la repo remota sparisce
	mv "$TRASH/srv/$1.git" "$TRASH/srv/$1.gone"
}
revive_srv() {
	mv "$TRASH/srv/$1.gone" "$TRASH/srv/$1.git"
}
# reader <nome> [<casata/nome del suo origin>]: collegato all'URL $SRC
reader() {
	make_repo "$1"
	cd "$TRASH/$1" || exit 1
	put o.txt o
	git shared link -c "$SRC" "$ID" >/dev/null 2>&1
	if [ -n "$2" ]; then
		git remote add origin "$(srv "$2")"
	fi
}

for R in luca/A luca/B luca/L anna/D paolo/F mario/C marco/G nessuno/N; do
	mkdir -p "$TRASH/srv/${R%/*}"
	git init -q --bare -b main "$TRASH/srv/$R.git"
done

# --- Luca crea da A ------------------------------------------------------------------
make_repo A
cd "$TRASH/A" || exit 1
git remote add origin "$TRASH/srv/luca/A.git"
SRVROOT=$(dirname "$(dirname "$(git remote get-url origin)")")
printf 'l1\n' >p.h
git add p.h
git commit -q -m p.h
git shared set -c p.h >/dev/null
ID=$(shared_id p.h)
git shared push origin p.h >/dev/null 2>&1
SRC=$(srv luca/A)
A=$(srv luca/A)
B=$(srv luca/B)
D=$(srv anna/D)
G=$(srv marco/G)
N=$(srv nessuno/N)
T0=$(tip A)
run git shared push
expect_rc "il creatore non si iscrive: codice 0" 0
expect_no_out "il creatore non si iscrive" "heir"
expect_eq "il creatore non si iscrive: nessuna voce" "$(srv_heirs luca/A)" ""

# --- iscrizioni ----------------------------------------------------------------------
reader B luca/B
run git shared push
expect_rc "B push: codice 0" 0
expect_out "B push: sul creatore" "p.h: pushed to the creator $A"
expect_out "B push: erede 1" "p.h: $B is now heir 1 of $A"
expect_out "B push: poi la copia" "p.h: versions copied to $B"
expect_eq "B push: la voce sul creatore" "$(srv_heirs luca/A)" "$(printf '%s\t%s' "$A" "$B")"
expect_eq "B push: stessa versione qui e sul creatore" "$(tip B)" "$(srv_tip luca/A)"
expect_eq "B push: e nella copia" "$(srv_tip luca/B)" "$(tip B)"
expect_eq "B push: genitore T" "$(git rev-parse "$(tip B)^1")" "$T0"
run git shared log
expect_out "B log: gli eredi cambiati, stesso contenuto" "same content; heirs changed"
expect_out "B log: il creatore comparso" "creator $A"
run git shared log --porcelain
expect_out "B log, porcelain: heirs" "heirs $(tip B)"
expect_out "B log, porcelain: authority-tip" "authority-tip $(tip B)"
expect_eq "B push: stesso contenuto" "$(git rev-parse "$(tip B):content")" \
	"$(git rev-parse "$T0:content")"
run git shared push
expect_no_out "B di nuovo: gia' erede" "is now heir"
expect_out "B di nuovo: niente da fare" "already on the creator"

reader L luca/L
run git shared push
expect_rc "L, stessa casata di B: codice 0" 0
expect_no_out "L, stessa casata di B: non si iscrive" "is now heir"

reader M mario/C
lock luca/A
run git shared push
expect_rc "lettore senza permessi: codice 0" 0
expect_out "lettore senza permessi: come prima" "already on the creator"
expect_no_out "lettore senza permessi: non e' erede" "is now heir"
expect_eq "lettore senza permessi: la sua catena non cambia" "$(tip M)" "$(tip B)"
unlock luca/A

reader X paolo/F
git config shared.heir false
run git shared push
expect_no_out "shared.heir false: non si iscrive" "is now heir"

SRC=$(srv luca/A)
reader D anna/D
run git shared push
expect_out "D push: erede 2" "p.h: $D is now heir 2 of $A"
expect_eq "D push: la voce" "$(srv_heirs luca/A)" \
	"$(printf '%s\t%s\n%s\t%s' "$A" "$B" "$A" "$D")"
cd "$TRASH/X" || exit 1
git config --unset shared.heir
git shared pull >/dev/null 2>&1
run git shared push
expect_no_out "nessun posto libero: non si iscrive" "is now heir"
run git shared status
expect_out "status: riga heirs" "    heirs  1 $B"
expect_out "status: secondo erede" "           2 $D"

# B riceve l'iscrizione di D; i lettori R e S arrivano adesso
cd "$TRASH/B" || exit 1
git shared pull >/dev/null 2>&1
reader R
reader S
expect_eq "R e S hanno gli eredi" "$(tip R)$(tip S)" "$(srv_tip luca/A)$(srv_tip luca/A)"

# --- il creatore sparisce ------------------------------------------------------------
kill_srv luca/A
cd "$TRASH/R" || exit 1
TR=$(tip R)
run git shared pull
expect_rc "nessun erede incoronato: codice 1" 1
expect_err "nessun erede incoronato: lo dice" "the authority and its heirs are unreachable"
expect_err "nessun erede incoronato: crown <url>" "git shared crown <url> p.h"
expect_err "nessun erede incoronato: crown della propria repo" "or your own remote:  'git shared crown p.h'"
expect_eq "nessun erede incoronato: T non cambia" "$(tip R)" "$TR"
run git shared status --remote --porcelain
expect_out "creatore morto, porcelain: unreachable" "unreachable $ID $A"
expect_out "creatore morto, porcelain: nessuno al suo posto" "succession $ID none 0 "
expect_out "creatore morto, porcelain: heir 1" "heir $ID 1 $B"
run git shared crown --yes p.h
expect_rc "crown senza URL e senza origin: codice 1" 1
expect_err "crown senza URL e senza origin: lo dice" "no remote named 'origin'"

cd "$TRASH/D" || exit 1
edit d1
git shared publish >/dev/null 2>&1
run git shared push
expect_rc "D push col creatore morto: codice 1" 1
expect_out "D push: la nota dell'erede" \
	"p.h: the creator $A is unreachable; you are heir 2: 'git shared crown p.h' to take over"

# --- Anna si incorona (crown senza URL: origin) --------------------------------------
run git shared crown --yes p.h
expect_rc "D si incorona: codice 0" 0
expect_out "D si incorona: sovrana" "p.h: pushed to $D, now the sovereign of the file"
expect_err "D si incorona: i lettori di A non lo sapranno" "readers of $A will not be told"

cd "$TRASH/R" || exit 1
run git shared pull
expect_rc "R pull: codice 0" 0
expect_out "R pull: segue l'erede da solo" "p.h: the authority moved to heir 2 $D ($A was unreachable)"
expect_out "R pull: lo segue" "p.h: now follows $D"
expect_eq "R pull: T come la sovrana" "$(tip R)" "$(srv_tip anna/D)"
expect_ok "R pull: il contenuto di D" grep -q d1 p.h
run git shared status
expect_out "R status: autorita' D" "authority $D"
expect_out "R status: resta Luca B, fratello" "    heirs  1 $B"

# Marco si iscrive sotto Anna
SRC=$D
reader G marco/G
run git shared push
expect_out "G push: erede 1 di D" "p.h: $G is now heir 1 of $D"
cd "$TRASH/R" || exit 1
git shared pull >/dev/null 2>&1
run git shared status
expect_out "R status: prima i figli di D" "    heirs  1 $G"
expect_out "R status: poi il fratello" "           2 $B"
run git shared status --porcelain
expect_eq "R status, porcelain: eredi in ordine" "$(printf '%s\n' "$OUT" | grep '^heir ')" \
	"heir $ID 1 $G
heir $ID 2 $B"

# --- cartello stradale: B segue Anna, S lo trova da B --------------------------------
cd "$TRASH/B" || exit 1
run git shared pull
expect_out "B pull: segue D" "p.h: the authority moved to heir 2 $D"
git shared push >/dev/null 2>&1
expect_eq "B push: la copia dichiara D" "$(srv_tip luca/B)" "$(tip B)"
cd "$TRASH/S" || exit 1
TS=$(tip S)
run git shared status --remote
expect_out "S status --remote: B dice dov'e' l'autorita'" \
	"$B says the authority is now heir 2 $D: 'git shared pull p.h' follows it"
expect_eq "S status --remote: non segue" "$(tip S)" "$TS"
run git shared status --remote --porcelain
expect_out "S, porcelain: succession found" "succession $ID found 2 $D"
expect_out "S, porcelain: succession-via" "succession-via $ID $B"

# --- il ritorno del creatore ---------------------------------------------------------
revive_srv luca/A
cd "$TRASH/R" || exit 1
run git shared status --remote
expect_out "Aslan: il creatore e' tornato" \
	"the creator $A is reachable again: 'git shared crown --restore p.h'"
expect_out "Aslan: l'autorita' resta D" "authority $D (same version"
run git shared status --remote --porcelain
expect_out "Aslan, porcelain: creator-back" "creator-back $ID $A"
expect_out "Aslan, porcelain: authority D" "authority $ID same "

# --- un'autorita' che non e' un erede ------------------------------------------------
kill_srv anna/D
cd "$TRASH/G" || exit 1
git shared crown --yes "$N" p.h >/dev/null 2>&1
git shared push >/dev/null 2>&1
expect_eq "G incorona N: la copia di G lo dice" "$(srv_tip marco/G)" "$(srv_tip nessuno/N)"
cd "$TRASH/R" || exit 1
TR=$(tip R)
run git shared pull
expect_rc "autorita' non erede: codice 1" 1
expect_err "autorita' non erede: lo dice" \
	"$G says it is now $N, which is not an heir: to follow it, 'git shared crown $N p.h'"
expect_eq "autorita' non erede: T non cambia" "$(tip R)" "$TR"
run git shared status --remote --porcelain
expect_out "autorita' non erede, porcelain: succession other" "succession $ID other 0 $N"
expect_out "autorita' non erede, porcelain: succession-via" "succession-via $ID $G"
run git shared crown --yes "$N" p.h
expect_rc "seguirla con crown: codice 0" 0
expect_out "seguirla con crown" "p.h: now follows $N"

done_testing
