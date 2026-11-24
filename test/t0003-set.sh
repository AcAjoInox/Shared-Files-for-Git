#!/bin/sh
# t0003-set.sh - set, conferma al commit, annullamento, sospensione
# (REQ-F-001, REQ-F-021, REQ-F-022, REQ-F-023, D-006, D-007, D-057).

. "$(dirname "$0")/lib.sh"

mkdir "$TRASH/norepo"
cd "$TRASH/norepo" || exit 1
run git shared set x.h
expect_rc "fuori da una repo: codice 128" 128

make_repo a
cd "$TRASH/a" || exit 1
mkdir -p inc "sub dir"
printf 'one\n' >inc/common.h
printf 'x\n' >"sub dir/a b.h"
printf 'u\n' >untracked.h
git add inc "sub dir"
git commit -q -m init

# --- argomenti ----------------------------------------------------------
run git shared set
expect_rc "senza path: codice 129" 129
expect_err "senza path: errore" "missing <path>"
run git shared set -x inc/common.h
expect_rc "opzione sconosciuta: codice 129" 129
run git shared set -m
expect_rc "-m senza valore: codice 129" 129
run git shared set inc/common.h altro
expect_rc "due path: codice 129" 129
run git shared set inc
expect_rc "cartella: codice 1" 1
expect_err "cartella: errore" "is a directory"
run git shared set untracked.h
expect_rc "file non tracciato: codice 1" 1
expect_err "file non tracciato: errore" "not tracked"
run git shared set 'inc/*'
expect_rc "glob non espanso (pathspec letterale): codice 1" 1

# --- set senza commit: tutto in attesa (REQ-F-023) ------------------------
run git shared set inc/common.h
expect_rc "set: codice 0" 0
expect_out "set: messaggio" "'inc/common.h' is now shared as 'common.h'"
ID=$(shared_id common.h)
expect_eq "set: ID di 32 cifre" "${#ID}" 32
case "$ID" in
*[!0-9a-f]*) not_ok "set: ID esadecimale minuscolo" "$ID" ;;
*) ok "set: ID esadecimale minuscolo" ;;
esac
expect_eq "set: UUID versione 4" "$(printf '%s' "$ID" | cut -c13)" 4
case "$(printf '%s' "$ID" | cut -c17)" in
[89ab]) ok "set: UUID variante RFC 4122" ;;
*) not_ok "set: UUID variante RFC 4122" "$ID" ;;
esac
expect_eq "set: path in .gitshared" \
	"$(git config -f .gitshared shared.common.h.path)" inc/common.h
expect_eq "set: branch di default in .gitshared" \
	"$(git config -f .gitshared shared.common.h.branch)" main
expect_ok "set: intestazione in .gitshared" grep -q "Managed by 'git shared'" .gitshared
expect_eq "set: .gitshared in staging" "$(git diff --cached --name-only)" .gitshared
expect_eq "set: attesa in .git/config" "$(pending_op "$ID")" set
expect_fail "set: nessuna catena prima del commit" git rev-parse -q --verify "refs/shared/$ID"

run git shared status
expect_out "prima del commit: in attesa" "pending commit"

run git shared set inc/common.h
expect_rc "set ripetuto: codice 1" 1
expect_err "set ripetuto: errore" "already shared as 'common.h'"

# --- commit dell'utente: il comando successivo completa ------------------
git commit -q -m "share common.h"
run git shared status
expect_rc "dopo il commit: codice 0" 0
expect_out "dopo il commit: allineato" "aligned"
expect_no_out "dopo il commit: non piu' in attesa" "pending"
expect_ok "catena creata" git rev-parse -q --verify "refs/shared/$ID"
expect_eq "catena: una sola voce 'content' con il blob di HEAD" \
	"$(git ls-tree "refs/shared/$ID")" \
	"$(printf '100644 blob %s\tcontent' "$(git rev-parse HEAD:inc/common.h)")"
expect_eq "catena: una sola versione" "$(git rev-list --count "refs/shared/$ID")" 1
expect_eq "catena: messaggio" "$(git log -1 --format=%s "refs/shared/$ID")" "shared: common.h"
expect_fail "attesa rimossa" git config "shared.$ID.pending"
expect_eq "branch attivi copiati dal default" "$(git config --get-all "shared.$ID.branch")" main

# --- stato dalla storia ----------------------------------------------------
printf 'two\n' >>inc/common.h
git commit -q -a -m mod
run git shared status
expect_out "modifica committata: da pubblicare" "to publish"
git reset -q --hard HEAD~1
run git shared status
expect_out "reset alla versione: allineato" "aligned"

# --- -c, da una sottocartella, path con spazi -----------------------------
cd "$TRASH/a/sub dir" || exit 1
run git shared set -c "a b.h"
expect_rc "set -c: codice 0" 0
expect_out "set -c: confermato" "'sub dir/a b.h' is now shared as 'a b.h'."
cd "$TRASH/a" || exit 1
expect_eq "set -c: messaggio standard" "$(git log -1 --format=%s)" "Share sub dir/a b.h"
expect_eq "set -c: il commit contiene solo .gitshared" \
	"$(git show --name-only --format= HEAD)" .gitshared
ID2=$(shared_id "a b.h")
expect_ok "set -c: catena creata subito" git rev-parse -q --verify "refs/shared/$ID2"

put y.h y
run git shared set -m "custom message" y.h
expect_rc "set -m: codice 0" 0
expect_eq "set -m: messaggio dato" "$(git log -1 --format=%s)" "custom message"

mkdir other
put other/common.h o
run git shared set other/common.h
expect_rc "nome gia' usato: codice 1" 1
expect_err "nome gia' usato: errore" "named 'common.h' already exists"

run git shared status
expect_rc "tre file: codice 0" 0
expect_no_out "tre file: nessuno in attesa" "pending"

# --- annullamento con git restore (REQ-F-023) -----------------------------
put z.h z
run git shared set z.h
IDZ=$(shared_id z.h)
git restore --staged .gitshared
git restore .gitshared
run git shared status
expect_err "restore: attesa scartata" "discarded pending operation"
expect_fail "restore: sezione locale rimossa" git config "shared.$IDZ.pending"
expect_no_out "restore: z.h non piu' elencato" "z.h"

# --- sospensione (REQ-F-022) -------------------------------------------------
git checkout -q --detach
run git shared status
expect_out "HEAD staccata: sospeso" "suspended on this branch (aligned)"
git checkout -q -b feature main
run git shared status
expect_out "branch non attivo: sospeso" "suspended on this branch"
git checkout -q main
run git shared status
expect_no_out "branch attivo: non sospeso" "suspended"

# --- -c con il file non ancora committato -----------------------------------
make_repo b
cd "$TRASH/b" || exit 1
put a.h a
printf 'n\n' >new.h
git add new.h
run git shared set -c new.h
expect_rc "file solo in staging: codice 0" 0
expect_err "file solo in staging: avviso" "not committed yet"
expect_no_out "file solo in staging: non confermato" "is now shared"
run git shared status
expect_out "file solo in staging: in attesa" "pending commit"
git commit -q -m "add new.h"
run git shared status
expect_out "file committato: allineato" "aligned"

# --- nomi con spazi e caratteri non ASCII (reportIdea C-01) ------------------
# Senza "ls-files -z" git restituiva "\"dir \303\250/perch\303\251.h\"".
make_repo u
cd "$TRASH/u" || exit 1
mkdir "dir è"
put "dir è/perché.h" p
run git shared set -c "dir è/perché.h"
expect_rc "nome non ASCII: codice 0" 0
expect_out "nome non ASCII: confermato" "'dir è/perché.h' is now shared as 'perché.h'"
expect_eq "nome non ASCII: path in .gitshared" \
	"$(git config -f .gitshared 'shared.perché.h.path')" "dir è/perché.h"
UID_=$(git config -f .gitshared 'shared.perché.h.id')
expect_ok "nome non ASCII: catena creata" git rev-parse -q --verify "refs/shared/$UID_"
run git shared status
expect_out "nome non ASCII: allineato" "aligned"
expect_no_out "nome non ASCII: niente ottale" '\303'
expect_eq "nome non ASCII: nessun avviso" "$ERR" ""

# Dalla sottocartella, con il path relativo.
put "dir è/città.h" c
cd "dir è" || exit 1
run git shared set "città.h"
expect_rc "non ASCII da sottocartella: codice 0" 0
expect_eq "non ASCII da sottocartella: path dalla radice" \
	"$(git config -f ../.gitshared 'shared.città.h.path')" "dir è/città.h"
cd .. || exit 1

done_testing
