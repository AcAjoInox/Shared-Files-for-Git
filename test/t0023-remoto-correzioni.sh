#!/bin/sh
# t0023-remoto-correzioni.sh - correzioni del prototipo remoto (D-112):
# ultima versione vista in refs/shared-authority/<id> con la chiave
# authoritySeen (URL e data, B-53); URL confrontati in forma normale
# (P-04); remote con path relativo (P-08); niente prompt delle credenziali
# (P-07); primo push che ritrova la propria versione (P-03); catena locale
# ricostruita (B-51); il namespace di v0.8.0 tolto da prune.
#
# Repo "remote" bare in $TRASH/srv, come in t0021.

. "$(dirname "$0")/lib.sh"

tip() {
	git -C "$TRASH/$1" rev-parse -q --verify "refs/shared/${2:-$ID}"
}
seen() {
	git -C "$TRASH/$1" rev-parse -q --verify "refs/shared-authority/${2:-$ID}"
}
srv_tip() {
	git --git-dir="$TRASH/srv/$1.git" rev-parse -q --verify "refs/shared/${2:-$ID}"
}
seen_key() {
	git -C "$TRASH/$1" config "shared.${2:-$ID}.authoritySeen"
}
creator_of() {
	git -C "$TRASH/$1" cat-file blob "refs/shared/${2:-$ID}:creator" 2>/dev/null
}
edit() {
	printf '%s\n' "$1" >>"${2:-p.h}"
	git add -- "${2:-p.h}"
	git commit -q -m "edit: $1" 2>/dev/null
}

mkdir "$TRASH/srv"
for R in A D F G; do
	git init -q --bare -b main "$TRASH/srv/$R.git"
done

# --- A crea, B peer locale di A ---------------------------------------------------
make_repo A
cd "$TRASH/A" || exit 1
git remote add origin "$TRASH/srv/A.git"
printf 'l1\nl2\n' >p.h
git add p.h
git commit -q -m p.h
git shared set -c p.h >/dev/null
ID=$(shared_id p.h)
SRV_A=$(git remote get-url origin)

make_repo B
cd "$TRASH/B" || exit 1
put o.txt o
git shared link -c ../A/p.h >/dev/null

# --- ultima versione vista: ref nuova e chiave authoritySeen (B-53) ---------------
cd "$TRASH/A" || exit 1
run git shared push origin p.h
expect_rc "primo push: codice 0" 0
expect_eq "la versione vista e' in refs/shared-authority" "$(seen A)" "$(tip A)"
expect_fail "niente piu' refs/shared-creator" \
	git rev-parse -q --verify "refs/shared-creator/$ID"
case "$(seen_key A)" in
[0-9]*" $SRV_A") ok "authoritySeen: data e URL" ;;
*) not_ok "authoritySeen: data e URL" "valore: '$(seen_key A)'" ;;
esac
run git shared status
expect_out "status: stessa versione con la data" "(same version, seen 20"

# Vista su un altro URL: per questo creatore e' come mai vista, e push
# non la prende per buona (A3).
git config "shared.$ID.authoritySeen" "1000 $TRASH/srv/altro.git"
run git shared status
expect_out "vista altrove: not checked yet" "(not checked yet"
expect_no_out "vista altrove: nessuna data" "seen 19"
run git shared push
expect_rc "vista altrove: push codice 0" 0
expect_out "vista altrove: push va al creatore" "p.h: pushed to the creator $SRV_A"
expect_no_out "vista altrove: non 'already'" "already on the creator"
case "$(seen_key A)" in
[0-9]*" $SRV_A") ok "push riscrive authoritySeen" ;;
*) not_ok "push riscrive authoritySeen" "valore: '$(seen_key A)'" ;;
esac
git config "shared.$ID.authoritySeen" "non valido"
run git shared status
expect_out "authoritySeen malformato: not checked yet" "(not checked yet"
run git shared status --remote
expect_rc "status --remote: codice 0" 0
expect_out "status --remote: data di nuovo" "(same version, seen 20"
case "$(seen_key A)" in
[0-9]*" $SRV_A") ok "status --remote riscrive authoritySeen" ;;
*) not_ok "status --remote riscrive authoritySeen" "valore: '$(seen_key A)'" ;;
esac

# --- il namespace di v0.8.0: ignorato, e prune lo toglie --------------------------
git update-ref "refs/shared-creator/$ID" "$(tip A)"
run git shared status
expect_out "ref vecchia: lo stato non cambia" "(same version, seen 20"
run git shared prune -n
expect_out "prune -n: la ref vecchia" "refs/shared-creator/$ID"
expect_no_out "prune -n: non la ref nuova" "refs/shared-authority/$ID"
run git shared prune
expect_rc "prune: codice 0" 0
expect_fail "prune: ref vecchia tolta anche con la sezione" \
	git rev-parse -q --verify "refs/shared-creator/$ID"
expect_eq "prune: la ref nuova resta" "$(seen A)" "$(tip A)"

# --- P-04: lo stesso path scritto in un'altra forma -------------------------------
# link dal creatore scritto con "/" finale: e' il creatore, la punta e' la
# versione vista.
make_repo C
cd "$TRASH/C" || exit 1
put o.txt o
run git shared link -c "$SRV_A/" "$ID"
expect_rc "link da \"$SRV_A/\": codice 0" 0
expect_eq "link con \"/\" finale: versione vista" "$(seen C)" "$(tip C)"
case "$(seen_key C)" in
[0-9]*" $SRV_A") ok "link: authoritySeen con l'URL della catena" ;;
*) not_ok "link: authoritySeen con l'URL della catena" "valore: '$(seen_key C)'" ;;
esac
case "$(uname -s)" in
MINGW* | MSYS* | CYGWIN*)
	# Windows: maiuscole e "\" non contano
	ALT=$(printf '%s' "$SRV_A" | tr 'A-Z/' 'a-z\\')
	git config --unset "shared.$ID.authoritySeen"
	git update-ref -d "refs/shared-authority/$ID"
	run git shared status --remote
	expect_out "status --remote: dopo, stessa versione" "(same version, seen 20"
	git config "shared.$ID.authoritySeen" "1000 $ALT"
	run git shared status
	expect_out "Windows: \"$ALT\" e' lo stesso creatore" "(same version, seen 19"
	;;
*) skip "Windows: maiuscole e \\ non contano" ;;
esac

# La copia sul proprio remote si salta se il remote e' il creatore, anche
# scritto in un'altra forma.
cd "$TRASH/B" || exit 1
git remote add origin "$SRV_A/"
git shared status >/dev/null 2>&1
edit l3
git shared publish >/dev/null
run git shared push
expect_rc "B push: codice 0" 0
expect_out "B push: al creatore" "p.h: pushed to the creator $SRV_A"
expect_no_out "B push: nessuna copia sul creatore stesso" "versions copied"
expect_eq "B push: il creatore ha la versione" "$(srv_tip A)" "$(tip B)"

# --- P-08: remote con path relativo -----------------------------------------------
make_repo D
cd "$TRASH/D" || exit 1
git remote add origin ../srv/D.git
printf 'd\n' >q.h
git add q.h
git commit -q -m q.h
git shared set -c q.h >/dev/null
IDQ=$(shared_id q.h)
run git shared push origin q.h
expect_rc "remote relativo: primo push codice 0" 0
CQ=$(creator_of D "$IDQ")
case "$CQ" in
/* | [A-Za-z]:/*) ok "remote relativo: creatore assoluto" ;;
*) not_ok "remote relativo: creatore assoluto" "creatore: '$CQ'" ;;
esac
case "$CQ" in
*..* | *'/./'*) not_ok "remote relativo: niente . e .." "creatore: '$CQ'" ;;
*) ok "remote relativo: niente . e .." ;;
esac
expect_eq "remote relativo: il creatore porta a srv/D.git" \
	"$(git ls-remote "$CQ" "refs/shared/$IDQ" | cut -f1)" "$(tip D "$IDQ")"
expect_out "remote relativo: messaggio con il path assoluto" "pushed to $CQ, which is now the creator"

# --- P-07: nessuna domanda di credenziali ------------------------------------------
# ssh finto: scrive GIT_TERMINAL_PROMPT come lo vede git, poi fallisce.
make_repo E
cd "$TRASH/E" || exit 1
git remote add origin "ssh://example.invalid/e.git"
git config core.sshCommand "echo \"\${GIT_TERMINAL_PROMPT-unset}\" >\"$TRASH/prompt\"; false"
printf 'e\n' >e.h
git add e.h
git commit -q -m e.h
git shared set -c e.h >/dev/null
run git shared push origin e.h
expect_rc "ssh che fallisce: codice 1" 1
expect_eq "ssh: GIT_TERMINAL_PROMPT=0" "$(cat "$TRASH/prompt" 2>/dev/null)" "0"

# --- P-03: la propria versione gia' sul remoto -------------------------------------
# Come se un primo push fosse riuscito e l'update-ref no: sul remoto c'e'
# la versione con il creatore, qui la catena e' ancora senza.
make_repo F
cd "$TRASH/F" || exit 1
git remote add origin "$TRASH/srv/F.git"
printf 'f\n' >s.h
git add s.h
git commit -q -m s.h
git shared set -c s.h >/dev/null
IDS=$(shared_id s.h)
URL_F=$(git remote get-url origin)
T=$(tip F "$IDS")
BLOB=$(printf '%s\n' "$URL_F" | git hash-object -w --stdin)
TREE=$(printf '100644 blob %s\tcontent\n100644 blob %s\tcreator\n' \
	"$(git rev-parse "$T:content")" "$BLOB" | git mktree)
R=$(GIT_COMMITTER_DATE='2001-01-01T00:00:00' git commit-tree "$TREE" -p "$T" -m "shared: s.h")
git push -q "$URL_F" "$R:refs/shared/$IDS"
run git shared push origin s.h
expect_rc "P-03: primo push di nuovo, codice 0" 0
expect_out "P-03: messaggio" "s.h: pushed to $URL_F, which is now the creator of the file"
expect_eq "P-03: la catena prende la versione del remoto" "$(tip F "$IDS")" "$R"
expect_eq "P-03: il remoto non cambia" "$(srv_tip F "$IDS")" "$R"
expect_eq "P-03: versione vista" "$(seen F "$IDS")" "$R"
expect_eq "P-03: nessuna ref temporanea" \
	"$(git for-each-ref "refs/shared-incoming/$IDS")" ""

# Una versione del remoto che non e' la propria (altro contenuto): rifiuto.
make_repo G
cd "$TRASH/G" || exit 1
git remote add origin "$TRASH/srv/G.git"
printf 'g\n' >u.h
git add u.h
git commit -q -m u.h
git shared set -c u.h >/dev/null
IDU=$(shared_id u.h)
URL_G=$(git remote get-url origin)
T=$(tip G "$IDU")
BLOB=$(printf '%s\n' "$URL_G" | git hash-object -w --stdin)
OTHER=$(printf 'altro\n' | git hash-object -w --stdin)
TREE=$(printf '100644 blob %s\tcontent\n100644 blob %s\tcreator\n' \
	"$OTHER" "$BLOB" | git mktree)
R=$(git commit-tree "$TREE" -p "$T" -m "shared: u.h")
git push -q "$URL_G" "$R:refs/shared/$IDU"
run git shared push origin u.h
expect_rc "versione altrui sul remoto: codice 1" 1
expect_eq "versione altrui: la catena resta senza creatore" "$(creator_of G "$IDU")" ""
expect_eq "versione altrui: il remoto non cambia" "$(srv_tip G "$IDU")" "$R"

# --- B-51: catena locale assente ---------------------------------------------------
# Remoto: dall'ultima versione vista, senza rete.
cd "$TRASH/C" || exit 1
git shared pull -c >/dev/null 2>&1
TC=$(tip C)
git update-ref -d "refs/shared/$ID"
run git shared status
expect_rc "catena assente, versione vista: codice 0" 0
expect_err "catena ricostruita dalla versione vista" \
	"p.h: the version chain was missing: restored from $SRV_A"
expect_eq "catena ricostruita: T" "$(tip C)" "$TC"
expect_out "catena ricostruita: stato" "state  aligned"
expect_no_out "catena ricostruita: nessuna nota" "version chain is missing"

# pull con la catena assente: prima la ricostruisce, poi il fetch.
git update-ref -d "refs/shared/$ID"
run git shared pull
expect_rc "pull con la catena assente: codice 0" 0
expect_err "pull: catena ricostruita" "the version chain was missing"
expect_eq "pull: T" "$(tip C)" "$(srv_tip A)"

# Locale: dal primo peer che ce l'ha.
cd "$TRASH/B" || exit 1

git update-ref -d "refs/shared/$ID"
git update-ref -d "refs/shared-authority/$ID"
run git shared status
expect_rc "catena assente, peer locale: codice 0" 0
expect_err "catena ricostruita da un peer" "p.h: the version chain was missing: restored from "
# la copia del peer, anche se piu' vecchia: la versione persa e' sul creatore
expect_eq "catena ricostruita dal peer: T del peer" "$(tip B)" "$(tip A)"
expect_eq "peer: nessuna ref temporanea" "$(git for-each-ref "refs/shared-incoming/$ID")" ""

# Nessuno ce l'ha: una nota dice l'unica uscita.
cd "$TRASH/C" || exit 1
git update-ref -d "refs/shared/$ID"
git update-ref -d "refs/shared-authority/$ID"
run git shared status
expect_out "nessuna copia: nota" \
	"the version chain is missing and no copy was found: run 'git shared unlink p.h'"
run git shared status --porcelain
expect_out "nessuna copia, porcelain: chain-missing" "chain-missing $ID"
expect_out "nessuna copia, porcelain: incoerente" "file $ID inconsistent active p.h"
expect_no_out "nessuna copia, porcelain: nessuna T" "tip $ID"
expect_fail "nessuna copia: la catena resta assente" \
	git rev-parse -q --verify "refs/shared/$ID"

# --- unlink toglie anche le ref vecchie e nuove ------------------------------------
cd "$TRASH/A" || exit 1
git update-ref "refs/shared-creator/$ID" "$(tip A)"
git shared unlink -c p.h >/dev/null 2>&1
expect_fail "unlink: ref nuova tolta" git rev-parse -q --verify "refs/shared-authority/$ID"
expect_fail "unlink: ref vecchia tolta" git rev-parse -q --verify "refs/shared-creator/$ID"

done_testing
