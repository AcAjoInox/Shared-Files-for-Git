#!/bin/sh
# t0011-prune.sh - git shared prune (REQ-F-014, D-082; reportIdea B-07,
# B-27, C-22).

. "$(dirname "$0")/lib.sh"

top() {
	git -C "$TRASH/$1" rev-parse --show-toplevel
}

# peers_of <repo> <id>: peer della repo, uno per riga, ordinati
peers_of() {
	git -C "$TRASH/$1" config --get-all "shared.$2.peer" | sort
}

# group <sorgente> <peer...>: la sorgente condivide p.h, gli altri lo collegano
group() {
	make_repo "$1"
	cd "$TRASH/$1" || exit 1
	put p.h v1
	git shared set -c p.h >/dev/null
	src=$1
	shift
	for R in "$@"; do
		make_repo "$R"
		cd "$TRASH/$R" || exit 1
		put o.txt o
		git shared link -c "../$src/p.h" >/dev/null
	done
}

# --- argomenti --------------------------------------------------------------------
group A B C
ID=$(cd "$TRASH/A" && shared_id p.h)
TOP_A=$(top A)
TOP_B=$(top B)
TOP_C=$(top C)
cd "$TRASH/A" || exit 1
run git shared prune a b
expect_rc "due argomenti: codice 129" 129
run git shared prune -x
expect_rc "opzione sconosciuta: codice 129" 129
run git shared prune nessuno.h
expect_rc "file non shared: codice 1" 1
run git shared prune -h
expect_out "uso: breve" "Forget the peers that no longer exist or no longer share the file."
run git shared prune --help
expect_out "manuale: prune" "prune never removes what may come back"

# --- niente da togliere ----------------------------------------------------------
run git shared prune
expect_rc "tutti presenti: codice 0" 0
expect_out "tutti presenti: niente" "Nothing to prune."
expect_eq "tutti presenti: peer intatti" "$(peers_of A "$ID")" "$(printf '%s\n%s' "$TOP_B" "$TOP_C" | sort)"

# --- peer cancellato --------------------------------------------------------------
rm -rf "$TRASH/C"
run git shared prune -n
expect_rc "-n: codice 0" 0
expect_out "-n: cosa toglierebbe" "p.h: would remove peer $TOP_C (it no longer exists)"
expect_eq "-n: niente tolto" "$(peers_of A "$ID")" "$(printf '%s\n%s' "$TOP_B" "$TOP_C" | sort)"
run git shared prune
expect_rc "cancellato: codice 0" 0
expect_out "cancellato: tolto" "p.h: removed peer $TOP_C (it no longer exists)"
expect_eq "cancellato: A conosce solo B" "$(peers_of A "$ID")" "$TOP_B"
expect_eq "cancellato: anche B lo dimentica" "$(peers_of B "$ID")" "$TOP_A"
run git shared prune
expect_out "ripetuto: niente" "Nothing to prune."
put p.h v2
run git shared publish
expect_rc "dopo prune: publish senza avvisi" 0
expect_eq "dopo prune: nessun peer irraggiungibile" "$ERR" ""

# --- cartella rimasta, repo tolta --------------------------------------------------
group D E F
ID=$(cd "$TRASH/D" && shared_id p.h)
rm -rf "$TRASH/F/.git"
cd "$TRASH/D" || exit 1
run git shared prune
expect_out "senza .git: tolto" "removed peer $(cd "$TRASH/F" && pwd -W 2>/dev/null || pwd) (it no longer exists)"
expect_eq "senza .git: resta E" "$(peers_of D "$ID")" "$(top E)"

# --- peer uscito mentre questa repo era irraggiungibile (B-07) ---------------------
group G H I
ID=$(cd "$TRASH/G" && shared_id p.h)
TOP_I=$(top I)
mv "$TRASH/G" "$TRASH/G.off"
cd "$TRASH/I" || exit 1
git shared unlink -c p.h >/dev/null 2>&1
mv "$TRASH/G.off" "$TRASH/G"
cd "$TRASH/G" || exit 1
expect_eq "uscito: G lo conosce ancora" "$(peers_of G "$ID" | grep -c "$TOP_I")" "1"
run git shared prune
expect_out "uscito: tolto" "p.h: removed peer $TOP_I (it no longer shares the file)"
expect_eq "uscito: G conosce solo H" "$(peers_of G "$ID")" "$(top H)"
expect_ok "uscito: la repo resta" test -d "$TRASH/I/.git"

# --- peer che non si legge adesso: resta (B-07) ------------------------------------
group J K L
ID=$(cd "$TRASH/J" && shared_id p.h)
TOP_L=$(top L)
cp "$TRASH/L/.git/config" "$TRASH/L.config"
printf 'non e[ un config valido\n' >"$TRASH/L/.git/config"
cd "$TRASH/J" || exit 1
run git shared prune
expect_rc "illeggibile: codice 0" 0
expect_err "illeggibile: avviso" "peer $TOP_L cannot be read now: kept"
expect_out "illeggibile: niente tolto" "Nothing to prune."
expect_eq "illeggibile: L resta" "$(peers_of J "$ID" | grep -c "$TOP_L")" "1"
cp "$TRASH/L.config" "$TRASH/L/.git/config"

# --- prune <path>: solo quel file ----------------------------------------------------
make_repo M
cd "$TRASH/M" || exit 1
put a.h a
put b.h b
git shared set -c a.h >/dev/null
git shared set -c b.h >/dev/null
AID=$(shared_id a.h)
BID=$(shared_id b.h)
make_repo N
cd "$TRASH/N" || exit 1
put o.txt o
git shared link -c ../M/a.h >/dev/null
git shared link -c ../M/b.h >/dev/null
cd "$TRASH/M" || exit 1
rm -rf "$TRASH/N"
run git shared prune a.h
expect_out "per path: tolto da a.h" "a.h: removed peer"
expect_no_out "per path: b.h non toccato" "b.h:"
expect_eq "per path: a.h senza peer" "$(git config --get-all "shared.$AID.peer")" ""
expect_eq "per path: b.h conserva il peer" "$(git config --get-all "shared.$BID.peer" | wc -l | tr -d ' ')" "1"

# --- ref rimaste senza sezione (B-27, C-22) ------------------------------------------
make_repo O
cd "$TRASH/O" || exit 1
put o.txt o
put c.h v1
git shared set -c c.h >/dev/null
CID=$(shared_id c.h)
FAKE=0123456789abcdef0123456789abcdef
git update-ref "refs/shared/$FAKE" HEAD
git update-ref "refs/shared-incoming/$FAKE/0" HEAD
git update-ref "refs/shared-incoming/$CID/0" HEAD
run git shared prune -n
expect_out "ref: -n le elenca" "would remove leftover refs/shared/$FAKE"
expect_ok "ref: -n non tocca" git rev-parse -q --verify "refs/shared/$FAKE"
run git shared prune
expect_out "ref: tolta la catena orfana" "removed leftover refs/shared/$FAKE"
expect_out "ref: tolta la ref temporanea orfana" "removed leftover refs/shared-incoming/$FAKE/0"
expect_fail "ref: catena orfana sparita" git rev-parse -q --verify "refs/shared/$FAKE"
expect_ok "ref: catena del file condiviso intatta" git rev-parse -q --verify "refs/shared/$CID"
expect_ok "ref: ref di un file con sezione intatta" git rev-parse -q --verify "refs/shared-incoming/$CID/0"
run git shared prune c.h
expect_out "ref: con <path> le ref non si toccano" "Nothing to prune."

# --- un link in attesa non si tocca --------------------------------------------------
make_repo P
cd "$TRASH/P" || exit 1
put o.txt o
git shared link ../O/c.h >/dev/null
run git shared prune
expect_out "link in attesa: niente" "Nothing to prune."
expect_ok "link in attesa: ref temporanea intatta" git rev-parse -q --verify "refs/shared-incoming/$CID/0"

done_testing
