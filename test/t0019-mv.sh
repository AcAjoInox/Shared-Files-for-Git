#!/bin/sh
# t0019-mv.sh - git shared mv (REQ-F-020, D-045, D-101): rinomina solo
# locale; oldpath in .gitshared; stato in attesa di commit finche' la
# rinomina non e' committata; storia che continua dal path di prima.

. "$(dirname "$0")/lib.sh"

# status_of <path>: riga dello stato del file <path> nell'output di status
status_of() {
	git shared status | awk -v p="$1" '
		$1 == "path" { cur = $2 }
		$1 == "state" && cur == p { sub(/^ *state +/, ""); print; exit }'
}

# --- A e B condividono common.h ------------------------------------------------
make_repo A
cd "$TRASH/A" || exit 1
mkdir inc
put inc/common.h v1
put other.c x
git shared set -c inc/common.h >/dev/null
ID=$(shared_id common.h)
make_repo B
cd "$TRASH/B" || exit 1
put b.c b
git shared link -c ../A/inc/common.h >/dev/null 2>&1
cd "$TRASH/A" || exit 1

# --- argomenti ----------------------------------------------------------------
run git shared mv
expect_rc "mv senza argomenti: codice 129" 129
expect_err "mv senza argomenti: manca il path" "missing <path>"
run git shared mv inc/common.h
expect_rc "mv senza destinazione: codice 129" 129
expect_err "mv senza destinazione: errore" "missing <destination>"
run git shared mv a b c
expect_rc "mv con tre argomenti: codice 129" 129
run git shared mv other.c x.c
expect_rc "mv di un file non shared: codice 1" 1
expect_err "mv di un file non shared: errore" "'other.c' is not a shared file"
run git shared mv inc/common.h inc/common.h
expect_rc "mv sullo stesso path: codice 1" 1
expect_err "mv sullo stesso path: errore" "already there"
run git shared mv inc/common.h other.c
expect_rc "mv su un file che esiste: codice 1" 1
expect_err "mv su un file che esiste: errore" "'other.c' already exists"
run git shared mv inc/common.h nuova/
expect_rc "mv in una cartella che non esiste: codice 1" 1
expect_err "mv in una cartella che non esiste: errore" "invalid destination path"
run git shared mv inc/common.h .gitshared
expect_rc "mv su .gitshared: codice 1" 1
run git shared mv -h
expect_rc "mv -h: codice 129" 129
expect_out "mv -h: uso" "usage: git shared mv"
expect_eq "nessun errore ha toccato il file" "$(git status --porcelain)" ""

# --- mv senza commit: in attesa di commit ----------------------------------------
run git shared mv inc/common.h inc/shared.h
expect_rc "mv: codice 0" 0
expect_out "mv: messaggio" "'inc/common.h' renamed to 'inc/shared.h'. Commit it with .gitshared to confirm."
expect_ok "mv: file al path nuovo" test -f inc/shared.h
expect_fail "mv: niente al path vecchio" test -e inc/common.h
expect_eq "mv: path nuovo in .gitshared" \
	"$(git config -f .gitshared shared.common.h.path)" "inc/shared.h"
expect_eq "mv: path vecchio tra gli oldpath" \
	"$(git config -f .gitshared --get-all shared.common.h.oldpath)" "inc/common.h"
expect_eq "mv: stesso nome e stesso ID" "$(shared_id common.h)" "$ID"
expect_eq "mv: in staging rinomina e .gitshared" \
	"$(git diff --cached --name-status | sort | tr '\t\n' '  ')" "M .gitshared R100 inc/common.h inc/shared.h "
expect_eq "mv: nessuna attesa in .git/config" "$(git config "shared.$ID.pending")" ""
run git shared status
expect_out "mv: stato in attesa di commit" "pending commit"
expect_out "mv: nota con il path di prima" "renamed from 'inc/common.h': commit it with .gitshared to confirm"
run git shared publish
expect_out "mv: publish salta il file" "inc/shared.h: pending commit, nothing to do"
run git shared update
expect_out "mv: update salta il file" "inc/shared.h: pending commit, nothing to do"
run git shared mv inc/shared.h inc/common.h
expect_rc "mv indietro prima del commit: codice 0" 0
expect_eq "mv avanti e indietro: path di prima" \
	"$(git config -f .gitshared shared.common.h.path)" "inc/common.h"
expect_eq "mv avanti e indietro: oldpath senza il path attuale" \
	"$(git config -f .gitshared --get-all shared.common.h.oldpath)" "inc/shared.h"
run git shared status
expect_out "mv avanti e indietro: allineato" "aligned"
git restore --staged --worktree .gitshared
run git shared mv inc/common.h inc/shared.h
expect_rc "mv di nuovo: codice 0" 0

# --- commit: allineato, peer non toccati -----------------------------------------
git commit -q -m "rinomina"
expect_eq "commit: allineato" "$(status_of inc/shared.h)" "aligned"
expect_eq "B: path invariato" "$(git -C ../B config -f .gitshared shared.common.h.path)" "inc/common.h"
expect_eq "B: allineato" "$(cd ../B && status_of inc/common.h)" "aligned"

# --- publish e update attraverso la rinomina -------------------------------------
put inc/shared.h v2
expect_eq "modifica dopo la rinomina: da pubblicare" "$(status_of inc/shared.h)" "to publish"
run git shared publish
expect_out "publish dal path nuovo" "inc/shared.h: 1 of 1 peers notified"
cd "$TRASH/B" || exit 1
run git shared update -c
expect_out "B riceve e applica al suo path" "inc/common.h: new version applied and committed."
expect_eq "B: contenuto nuovo" "$(cat inc/common.h)" "v2"
put inc/common.h v3
git shared publish >/dev/null
cd "$TRASH/A" || exit 1
run git shared update -c
expect_out "A applica al path nuovo" "inc/shared.h: new version applied and committed."
expect_eq "A: contenuto di B" "$(cat inc/shared.h)" "v3"

# --- la storia continua dal path di prima (D-101) ---------------------------------
# Modifica committata e non pubblicata, poi rinomina con -c: resta da
# pubblicare (senza oldpath sarebbe divergente senza base).
printf 'v3\nriga di A\n' >inc/shared.h
git commit -q -a -m "A aggiunge una riga"
run git shared mv -c inc/shared.h lib/common.h
expect_rc "mv -c in una cartella che non c'e': codice 1" 1
expect_err "mv -c in una cartella che non c'e': errore" "folder 'lib' does not exist"
mkdir lib
run git shared mv -c inc/shared.h lib/
expect_rc "mv -c in una cartella: codice 0" 0
expect_out "mv -c: confermato" "'inc/shared.h' renamed to 'lib/shared.h'."
expect_eq "mv -c: messaggio standard" "$(git log -1 --format=%s)" "Rename inc/shared.h to lib/shared.h"
expect_eq "mv -c: il commit contiene .gitshared e la rinomina" \
	"$(git show --name-status --format= HEAD | sort | tr '\t\n' '  ')" "M .gitshared R100 inc/shared.h lib/shared.h "
expect_eq "mv -c: oldpath con entrambi i path di prima" \
	"$(git config -f .gitshared --get-all shared.common.h.oldpath | tr '\n' ' ')" "inc/common.h inc/shared.h "
expect_eq "modifica non pubblicata dopo mv: ancora da pubblicare" "$(status_of lib/shared.h)" "to publish"

# Divergenza attraverso la rinomina: la base (v3) c'e' solo al path di
# prima. B pubblica, A unisce senza conflitti.
cd "$TRASH/B" || exit 1
printf 'riga di B\nv3\n' >inc/common.h
git commit -q -a -m "B"
git shared publish >/dev/null
cd "$TRASH/A" || exit 1
expect_eq "rinomina e divergenza: divergente" "$(status_of lib/shared.h)" "diverged"
run git shared update -c
expect_rc "update unisce con la base di prima della rinomina: codice 0" 0
expect_out "update: unito" "lib/shared.h: merged and committed"
expect_eq "update: nessun conflitto, entrambe le righe" "$(tr '\n' ' ' <lib/shared.h)" "riga di B v3 riga di A "

# Rinomina e modifica nello stesso commit.
run git shared publish
git shared mv lib/shared.h lib/common.h >/dev/null
printf 'riga in piu\n' >>lib/common.h
git add lib/common.h
git commit -q -m "rinomina e modifica"
expect_eq "rinomina e modifica nello stesso commit: da pubblicare" "$(status_of lib/common.h)" "to publish"
git shared publish >/dev/null

# --- file gia' rinominato con git mv ------------------------------------------------
git mv lib/common.h lib/c.h
git commit -q -m "git mv"
expect_eq "git mv senza git shared: incoerente" "$(status_of lib/common.h)" "inconsistent"
run git shared mv lib/common.h lib/c.h
expect_rc "mv dopo git mv: codice 0" 0
expect_eq "mv dopo git mv: path in .gitshared" \
	"$(git config -f .gitshared shared.common.h.path)" "lib/c.h"
expect_eq "mv dopo git mv: solo .gitshared in staging" "$(git diff --cached --name-only)" ".gitshared"
git commit -q -m "path in .gitshared"
expect_eq "mv dopo git mv: allineato" "$(status_of lib/c.h)" "aligned"
run git shared mv -c lib/missing.h lib/x.h
expect_rc "mv di un path che non c'e': codice 1" 1

# --- casi rifiutati ---------------------------------------------------------------
make_repo C
cd "$TRASH/C" || exit 1
put c.h c
put d.h d
git shared set -c d.h >/dev/null
git shared set c.h >/dev/null
run git shared mv c.h e.h
expect_rc "mv con un set in attesa: codice 1" 1
expect_err "mv con un set in attesa: errore" "has a pending operation"
git commit -q -m "set c.h"
run git shared mv c.h d.h
expect_rc "mv su un altro file shared: codice 1" 1
expect_err "mv su un altro file shared: errore" "'d.h' is already shared as 'd.h'"

# Clone: file scollegato, la rinomina vale lo stesso.
git clone -q "$TRASH/C" "$TRASH/C2"
cd "$TRASH/C2" || exit 1
run git shared mv -c c.h e.h
expect_rc "mv in un clone (scollegato): codice 0" 0
expect_eq "mv in un clone: path in .gitshared" "$(git config -f .gitshared shared.c.h.path)" "e.h"

# Unione in corso: non si sposta.
cd "$TRASH/B" || exit 1
git shared update -c >/dev/null 2>&1
printf 'conflitto di B\n' >inc/common.h
git commit -q -a -m "B conflitto"
git shared publish >/dev/null 2>&1
cd "$TRASH/A" || exit 1
printf 'conflitto di A\n' >lib/c.h
git commit -q -a -m "A conflitto"
git shared update >/dev/null 2>&1
run git shared mv lib/c.h lib/d.h
expect_rc "mv con un'unione in corso: codice 1" 1
expect_err "mv con un'unione in corso: errore" "has a merge in progress"
git shared update --abort >/dev/null 2>&1

done_testing
