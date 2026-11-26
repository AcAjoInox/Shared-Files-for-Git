#!/bin/sh
# t0033-remoto-url.sh - l'URL del remote che va nella catena e quello
# usato per il trasporto (S-01, AB-02, AB-03 di report/26-11-26_auditBug).
#
# Nella catena: l'URL come e' configurato, non quello riscritto da
# url.<base>.insteadOf; senza credenziali (token o utente:password), mai
# stampate. Per il trasporto: verso l'URL di un remote configurato git
# riceve il nome del remote, quindi valgono pushurl, insteadOf e le
# credenziali scritte nell'URL configurato.
#
# Gli URL sono https://gs.invalid/luca/<R>.git: una regola insteadOf
# globale li porta alle repo bare in $TRASH/srv, come farebbe chi riscrive
# https in ssh solo sulla propria macchina.

. "$(dirname "$0")/lib.sh"

git config --global shared.heir false

srv_tip() {
	git --git-dir="$TRASH/srv/$1.git" rev-parse -q --verify "refs/shared/${2:-$ID}"
}
blob_of() {
	git -C "$TRASH/$1" cat-file blob "refs/shared/${3:-$ID}:$2" 2>/dev/null
}
edit() {
	printf '%s\n' "$1" >>"${2:-p.h}"
	git add -- "${2:-p.h}"
	git commit -q -m "edit: $1" 2>/dev/null
}
# no_secret <descrizione> <testo>: ne' stdout ne' stderr contengono il testo
no_secret() {
	case "$OUT$ERR" in
	*"$2"*) not_ok "$1" "l'uscita contiene: $2" ;;
	*) ok "$1" ;;
	esac
}

mkdir "$TRASH/srv"
for R in A B E F F-push G H N; do
	git init -q --bare -b main "$TRASH/srv/$R.git"
done
URL=https://gs.invalid/luca
git config --global url."$TRASH/srv/".insteadOf "$URL/"
git config --global --add url."$TRASH/srv/".insteadOf "https://tok123@gs.invalid/luca/"
git config --global --add url."$TRASH/srv/".insteadOf "https://u:secret@gs.invalid/luca/"
git config --global --add url."$TRASH/srv/".insteadOf "https://tok999@gs.invalid/luca/"
git config --global --add url."$TRASH/srv/".insteadOf "gs:luca/"

# new_file <repo> <remote url> <file>: repo con origin e un file shared
new_file() {
	make_repo "$1"
	cd "$TRASH/$1" || exit 1
	git remote add origin "$2"
	printf 'l1\n' >"$3"
	git add "$3"
	git commit -q -m "$3"
	git shared set -c "$3" >/dev/null
}

# --- AB-02: nella catena l'URL configurato, non quello di insteadOf ---------------
new_file A "$URL/A.git" p.h
ID=$(shared_id p.h)
run git shared push origin p.h
expect_rc "insteadOf: primo push riuscito" 0
expect_eq "insteadOf: creatore = URL configurato" "$(blob_of A creator)" "$URL/A.git"
expect_eq "insteadOf: il push e' andato dove porta la regola" \
	"$(srv_tip A)" "$(git rev-parse refs/shared/$ID)"
expect_out "insteadOf: il messaggio dice l'URL configurato" "pushed to $URL/A.git"

# un lettore collegato da quell'URL riceve dal creatore
make_repo C
cd "$TRASH/C" || exit 1
put o.txt o
run git shared link -c "$URL/A.git" "$ID"
expect_rc "insteadOf: link dall'URL della catena" 0
cd "$TRASH/A" || exit 1
edit l2
git shared publish >/dev/null
run git shared push
expect_rc "insteadOf: push successivo all'autorita'" 0
cd "$TRASH/C" || exit 1
run git shared pull -c p.h
expect_rc "insteadOf: pull del lettore" 0
expect_eq "insteadOf: il lettore ha la versione nuova" "$(tr '\n' ' ' <p.h)" "l1 l2 "

# abbreviazione: l'URL configurato non e' completo, resta quello espanso
new_file G "gs:luca/G.git" g.h
IDG=$(shared_id g.h)
run git shared push origin g.h
expect_rc "abbreviazione: primo push riuscito" 0
case "$(blob_of G creator "$IDG")" in
gs:*) not_ok "abbreviazione: nella catena l'URL espanso" "creatore: $(blob_of G creator "$IDG")" ;;
*srv/G.git) ok "abbreviazione: nella catena l'URL espanso" ;;
*) not_ok "abbreviazione: nella catena l'URL espanso" "creatore: $(blob_of G creator "$IDG")" ;;
esac

# --- S-01: credenziali tolte, mai stampate ------------------------------------
new_file B "https://tok123@gs.invalid/luca/B.git" b.h
IDB=$(shared_id b.h)
run git shared push origin b.h
expect_rc "token: primo push riuscito" 0
expect_eq "token: creatore senza token" "$(blob_of B creator "$IDB")" "$URL/B.git"
expect_err "token: avviso" "contains credentials"
no_secret "token: mai stampato" "tok123"
expect_eq "token: il push e' arrivato (credenziali del remote)" \
	"$(srv_tip B "$IDB")" "$(git rev-parse refs/shared/$IDB)"
edit l2 b.h
git shared publish >/dev/null
run git shared push
expect_rc "token: push successivo (autorita' senza token -> remote origin)" 0
expect_eq "token: versione nuova sull'autorita'" \
	"$(srv_tip B "$IDB")" "$(git rev-parse refs/shared/$IDB)"
no_secret "token: mai stampato nel push successivo" "tok123"

new_file E "https://u:secret@gs.invalid/luca/E.git" e.h
IDE=$(shared_id e.h)
run git shared push origin e.h
expect_rc "utente:password: primo push riuscito" 0
expect_eq "utente:password: creatore senza userinfo" "$(blob_of E creator "$IDE")" "$URL/E.git"
no_secret "utente:password: password mai stampata" "secret"

# eredi: l'URL del proprio remote, scritto a ogni push, senza token
git config --global shared.heir true
make_repo H
cd "$TRASH/H" || exit 1
git remote add origin "https://tok999@gs.invalid/luca/H.git"
put o.txt o
git shared link -c "$URL/A.git" "$ID" >/dev/null 2>&1
edit h1
git shared publish >/dev/null
run git shared push
expect_rc "erede: push riuscito" 0
expect_eq "erede: iscritto con l'URL senza token" \
	"$(git cat-file blob "refs/shared/$ID:heirs" 2>/dev/null)" "$(printf '%s\t%s' "$URL/A.git" "$URL/H.git")"
no_secret "erede: token mai stampato" "tok999"
git config --global shared.heir false

# crown con un URL scritto con il token: nella catena senza
cd "$TRASH/A" || exit 1
git shared pull -c p.h >/dev/null 2>&1
run git shared crown --yes "https://tok123@gs.invalid/luca/N.git" p.h
expect_rc "crown <url con token>: riuscito" 0
expect_err "crown <url con token>: avviso" "URL given contains credentials"
expect_eq "crown <url con token>: sovrano senza token" "$(blob_of A sovereigns)" "$URL/N.git"
no_secret "crown <url con token>: token mai stampato" "tok123"

# --- AB-03: push verso il pushurl del remote -----------------------------------
new_file F "$URL/F.git" f.h
IDF=$(shared_id f.h)
git remote set-url --push origin "$URL/F-push.git"
run git shared push origin f.h
expect_rc "pushurl: primo push riuscito" 0
expect_eq "pushurl: nella catena l'URL di fetch" "$(blob_of F creator "$IDF")" "$URL/F.git"
expect_eq "pushurl: la versione e' sul pushurl" \
	"$(srv_tip F-push "$IDF")" "$(git rev-parse refs/shared/$IDF)"
expect_eq "pushurl: niente sull'URL di fetch" "$(srv_tip F "$IDF")" ""

done_testing
