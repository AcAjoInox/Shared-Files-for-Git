#!/bin/sh
# t0034-remoto-bassi.sh - i problemi di gravita' bassa del remoto
# (report/bug-aperti.md): forma normale stretta per le decisioni (S-06,
# B-SA1), testo non fidato (S-07), path di default di link remoto (S-08),
# fetch con fsck (S-09), niente prompt di ssh e GCM (S-10), catena
# ricostruita (S-11), crown --accept di un rifiuto (S-12, B-SA4), forma
# delle voci (B-SA6), ref temporanea di crown (B-SA3), link <remote|path>
# (AB-06), testi della visita degli eredi (AB-07), segno "authority" di
# log (AB-08), avviso di push (AB-09).
#
# Molte prove mettono a mano, con mktree e commit-tree, una versione
# "malevola" nella ref dell'ultima versione vista (refs/shared-authority):
# la ricezione la tratta come una versione arrivata dall'autorita', senza
# rete.

. "$(dirname "$0")/lib.sh"

git config --global shared.heir false
unset GIT_SSH GIT_SSH_COMMAND

tip() {
	git -C "$TRASH/$1" rev-parse -q --verify "refs/shared/${2:-$ID}"
}
edit() {
	printf '%s\n' "$1" >>"${2:-p.h}"
	git add -- "${2:-p.h}"
	git commit -q -m "edit: $1" 2>/dev/null
}
# blob <testo>: blob "<testo>\n" nella repo corrente
blob() {
	printf '%s\n' "$1" | git hash-object -w --stdin
}
# mkver <genitore> <blob creator> <blob sovereigns|-> <blob heirs|-> [<messaggio>]:
# versione con il contenuto del genitore e le voci date
mkver() {
	_tree=$(
		{
			printf '100644 blob %s\tcontent\n' "$(git rev-parse "$1:content")"
			printf '100644 blob %s\tcreator\n' "$2"
			[ "$3" = - ] || printf '100644 blob %s\tsovereigns\n' "$3"
			[ "$4" = - ] || printf '100644 blob %s\theirs\n' "$4"
		} | git mktree
	)
	git commit-tree "$_tree" -p "$1" -m "${5:-shared: p.h}"
}
# setseen <versione> [<url>]: ultima versione vista, sull'URL dato
setseen() {
	git update-ref "refs/shared-authority/$ID" "$1"
	git config "shared.$ID.authoritySeen" "1 ${2:-$SRV_A}"
}
# reader <nome>: repo nuova collegata al file con l'URL dell'autorita'
reader() {
	make_repo "$1"
	cd "$TRASH/$1" || exit 1
	put o.txt o
	git shared link -c "$SRV_A" "$ID" >/dev/null 2>&1
}

mkdir "$TRASH/srv"
for R in A N N2; do
	git init -q --bare -b main "$TRASH/srv/$R.git"
done

# A crea il file, due versioni sull'autorita'
make_repo A
cd "$TRASH/A" || exit 1
git remote add origin "$TRASH/srv/A.git"
printf 'l1\n' >p.h
git add p.h
git commit -q -m p.h
git shared set -c p.h >/dev/null
ID=$(shared_id p.h)
git shared push origin p.h >/dev/null 2>&1
V1=$(tip A)
edit l2
git shared publish >/dev/null 2>&1
git shared push >/dev/null 2>&1
V2=$(tip A)
SRV_A=$(git cat-file blob "refs/shared/$ID:creator")

reader R
T0=$(tip R)
expect_eq "R collegato all'ultima versione" "$T0" "$V2"
CR=$(git rev-parse "$T0:creator")

# --- S-11: catena ricostruita fino all'ultima versione con l'autorita' vista ---------
V=$(mkver "$T0" "$CR" "$(blob "$TRASH/srv/X.git")" -)
setseen "$V"
git update-ref -d "refs/shared/$ID"
run git shared status
expect_eq "S-11: T ricostruita senza il cambio di autorita'" "$(tip R)" "$T0"
expect_out "S-11: il cambio resta da decidere" "authority -> $TRASH/srv/X.git"
setseen "$T0"

# --- B-SA6: la voce creator in un'altra forma non vale ----------------------------
V=$(mkver "$T0" "$(printf '%s\n\n' "$SRV_A" | git hash-object -w --stdin)" - -)
setseen "$V"
run git shared status
expect_eq "B-SA6: creator con due a capo: T non avanza" "$(tip R)" "$T0"
setseen "$T0"

# --- S-07: URL della catena solo stampabili, non ASCII solo nei path --------------
CYR=$(printf 'https://gs.invalid/lu\321\201a/A.git')
BIDI=$(printf 'https://gs.invalid/luca/\342\200\256A.git')
for U in "$CYR" "$BIDI"; do
	V=$(mkver "$T0" "$CR" "$(blob "$U")" -)
	setseen "$V"
	run git shared status
	expect_eq "S-07: sovrano non ASCII: T non avanza" "$(tip R)" "$T0"
	expect_no_out "S-07: sovrano non ASCII: nessun cambio da decidere" "authority ->"
done
setseen "$T0"
run git shared crown --yes "$CYR" p.h
expect_rc "S-07: crown con un URL cirillico: codice 1" 1
expect_err "S-07: crown con un URL cirillico: lo dice" "neither a remote of this repository nor a usable URL"

# un path locale non ASCII resta valido come autorita'
JOSE=$(printf '%s/srv/Jos\303\251.git' "$TRASH")
git init -q --bare -b main "$JOSE"
make_repo J
cd "$TRASH/J" || exit 1
git remote add origin "$JOSE"
put j.h j
git shared set -c j.h >/dev/null
run git shared push origin j.h
expect_rc "S-07: path non ASCII come autorita': push riuscito" 0
case "$(git cat-file blob "refs/shared/$(shared_id j.h):creator")" in
*"Jos"*) ok "S-07: path non ASCII nella catena" ;;
*) not_ok "S-07: path non ASCII nella catena" "$(git cat-file blob "refs/shared/$(shared_id j.h):creator")" ;;
esac

# --- S-07: nome del file e autore senza controlli per il terminale ----------------
ESC=$(printf '\033')
cd "$TRASH/A" || exit 1
VN=$(git commit-tree "$(git rev-parse "$V2^{tree}")" -p "$V2" -m "shared: e${ESC}[31mvil.h")
git push -q "$TRASH/srv/N.git" "$VN:refs/shared/$ID"
make_repo L1
cd "$TRASH/L1" || exit 1
put o.txt o
run git shared link "$TRASH/srv/N.git" "$ID" e.h
expect_rc "S-07: link da una copia con un nome con ESC: codice 0" 0
case "$OUT$ERR" in
*"$ESC"*) not_ok "S-07: nessun ESC stampato" ;;
*) ok "S-07: nessun ESC stampato" ;;
esac
case "$(cat .gitshared)" in
*"$ESC"*) not_ok "S-07: nessun ESC in .gitshared" ;;
*'e?[31mvil.h'*) ok "S-07: nome ripulito in .gitshared" ;;
*) not_ok "S-07: nome ripulito in .gitshared" "$(cat .gitshared)" ;;
esac
# senza <dest> il nome ripulito (con '?') non e' un nome di file semplice
make_repo L1b
cd "$TRASH/L1b" || exit 1
put o.txt o
run git shared link "$TRASH/srv/N.git" "$ID"
expect_rc "S-07, S-08: nome ripulito senza <dest>: codice 1" 1
expect_err "S-07, S-08: chiede la destinazione" "is not a plain file name"

cd "$TRASH/R" || exit 1
VA=$(GIT_AUTHOR_NAME="Ev${ESC}]0;titolo${ESC}il" git commit-tree "$(git rev-parse "$T0^{tree}")" \
	-p "$T0" -m "shared: p.h")
setseen "$VA"
run git shared log p.h
case "$OUT" in
*"$ESC"*) not_ok "S-07: log senza ESC nell'autore" ;;
*'Ev?]0;titolo?il'*) ok "S-07: log con l'autore ripulito" ;;
*) not_ok "S-07: log con l'autore ripulito" "$OUT" ;;
esac
git update-ref "refs/shared/$ID" "$T0"
setseen "$T0"

# --- S-08: senza <dest>, solo un nome di file semplice ----------------------------
cd "$TRASH/A" || exit 1
VG=$(git commit-tree "$(git rev-parse "$V2^{tree}")" -p "$V2" -m "shared: .github/workflows/ci.yml")
git push -q "$TRASH/srv/N2.git" "$VG:refs/shared/$ID"
make_repo L2
cd "$TRASH/L2" || exit 1
put o.txt o
run git shared link "$TRASH/srv/N2.git" "$ID"
expect_rc "S-08: nome con una cartella: codice 1" 1
expect_err "S-08: lo dice" "is not a plain file name"
expect_fail "S-08: nessun file scritto" test -e .github
expect_fail "S-08: nessun .gitshared" test -e .gitshared
expect_eq "S-08: nessuna attesa" "$(pending_op "$ID")" ""
run git shared link "$TRASH/srv/N2.git" "$ID" ci.yml
expect_rc "S-08: con <dest>: codice 0" 0
expect_ok "S-08: con <dest>: il file c'e'" test -f ci.yml

# --- S-06, B-SA1: forma normale stretta per le decisioni --------------------------
# T con un creatore di rete (messa a mano: la rete non serve, status e'
# locale); la versione vista sposta l'autorita' su un URL che la forma
# larga confonderebbe con quello di T.
cd "$TRASH/R" || exit 1
# strict_case <creatore di T> <sovrano della versione vista> <hold|keep> <descrizione>
strict_case() {
	_t=$(mkver "$T0" "$(blob "$1")" - -)
	git update-ref "refs/shared/$ID" "$_t"
	git config --unset-all "shared.$ID.authorityRejected" 2>/dev/null
	git config --unset-all "shared.$ID.authorityAccepted" 2>/dev/null
	_v=$(mkver "$_t" "$(git rev-parse "$_t:creator")" "$(blob "$2")" -)
	setseen "$_v" "$1"
	run git shared status
	if [ "$3" = hold ]; then
		expect_eq "$4: T non avanza" "$(tip R)" "$_t"
		expect_out "$4: cambio da decidere" "authority -> $2"
	else
		expect_eq "$4: stessa repo, T avanza" "$(tip R)" "$_v"
	fi
}
strict_case "https://gs.invalid/luca/A.git" "https://gs.invalid:8443/luca/A.git" hold \
	"S-06 porta diversa"
strict_case "ssh://alice@gs.invalid/srv/repo" "ssh://bob@gs.invalid/srv/repo" hold \
	"S-06 utente ssh diverso"
strict_case "https://gs.invalid/srv/Repo" "https://gs.invalid/srv/repo.git" hold \
	"S-06 maiuscole e .git su un host qualunque"
strict_case "ssh://git@[2001:db8::1]/r" "ssh://git@[2001:db8::2]/r" hold \
	"B-SA1 IPv6 diversi"
strict_case "https://github.com/luca/A" "git@github.com:luca/A.git" keep \
	"S-06 github: https e ssh"
strict_case "https://gs.invalid/luca/A.git" "https://gs.invalid:443/luca/A.git/" keep \
	"S-06 porta di default e / finale"
git update-ref "refs/shared/$ID" "$T0"
git config --unset-all "shared.$ID.authorityRejected" 2>/dev/null
setseen "$T0"

# --- AB-08: il segno authority solo sulla versione vista sull'autorita' di T -------
setseen "$V1"
run git shared log --porcelain p.h
expect_out "AB-08: vista sull'autorita': authority-tip" "authority-tip $V1"
setseen "$V1" "$TRASH/srv/altro.git"
run git shared log --porcelain p.h
expect_no_out "AB-08: vista su un'altra autorita': nessun segno" "authority-tip"
setseen "$T0"

# --- B-SA3: crown non tocca la ref temporanea di un link (indice 0) ----------------
git update-ref "refs/shared-incoming/$ID/0" "$T0"
run git shared crown --restore p.h
expect_eq "B-SA3: la ref .../0 resta" "$(git rev-parse -q --verify "refs/shared-incoming/$ID/0")" "$T0"
expect_eq "B-SA3: nessuna ref temporanea rimasta" \
	"$(git for-each-ref --format='%(refname)' "refs/shared-incoming/$ID" | grep -v '/0$')" ""
git update-ref -d "refs/shared-incoming/$ID/0"

# --- S-09: oggetti controllati da git al fetch ------------------------------------
BAD=$(printf 'tree %s\nparent %s\ncommitter C <c@example.com> 1 +0000\n\nshared: p.h\n' \
	"$(git rev-parse "$T0^{tree}")" "$T0" |
	git --git-dir="$TRASH/srv/A.git" hash-object -t commit --literally -w --stdin)
git --git-dir="$TRASH/srv/A.git" update-ref "refs/shared/$ID" "$BAD"
run git shared pull p.h
expect_rc "S-09: commit senza autore: pull codice 1" 1
expect_eq "S-09: la versione vista non cambia" "$(git rev-parse "refs/shared-authority/$ID")" "$T0"
expect_fail "S-09: l'oggetto non e' entrato" git cat-file -e "$BAD"
git --git-dir="$TRASH/srv/A.git" update-ref "refs/shared/$ID" "$V2"

# --- AB-07: autorita' che risponde senza il file, nessun erede --------------------
git --git-dir="$TRASH/srv/A.git" update-ref -d "refs/shared/$ID"
run git shared pull p.h
expect_rc "AB-07: autorita' senza il file: codice 1" 1
expect_err "AB-07: risponde ma non ha il file" "answers but no longer has the file"
expect_err "AB-07: nessun erede" "there are no heirs in the chain"
run git shared status --remote
expect_out "AB-07 status: nota sul file sparito" "no longer has the file"
expect_out "AB-07 status: nessun erede" "there are no heirs in the chain"
run git shared status --remote --porcelain
expect_out "AB-07 porcelain: nessuno ha risposto, linea vuota" "succession-answer $ID 0 0"
git --git-dir="$TRASH/srv/A.git" update-ref "refs/shared/$ID" "$V2"

# eredi che non rispondono
reader R8
TH=$(mkver "$(tip R8)" "$(git rev-parse "$(tip R8):creator")" - \
	"$(printf '%s\t%s\n' "$SRV_A" "$TRASH/srv/gone.git" | git hash-object -w --stdin)")
git update-ref "refs/shared/$ID" "$TH"
mv "$TRASH/srv/A.git" "$TRASH/srv/A.off"
run git shared pull p.h
expect_rc "AB-07: eredi muti: codice 1" 1
expect_err "AB-07: eredi muti: lo dice" "none of the 1 addresses of the line of succession answers"
expect_no_out "AB-07: eredi muti: niente file sparito" "no longer has the file"
mv "$TRASH/srv/A.off" "$TRASH/srv/A.git"

# --- AB-06: link <remote|path relativo> risolto come crown -------------------------
make_repo R6
cd "$TRASH/R6" || exit 1
put o.txt o
mkdir sub
cd sub || exit 1
run git shared link -c ../../srv/A.git "$ID"
expect_rc "AB-06: path relativo da una sottocartella: codice 0" 0
expect_no_out "AB-06: path relativo: non e' una copia" "is a copy"
expect_ok "AB-06: path relativo: ultima versione vista registrata" \
	git config "shared.$ID.authoritySeen"
make_repo R7
cd "$TRASH/R7" || exit 1
put o.txt o
git remote add up "$TRASH/srv/A.git"
run git shared link -c up "$ID"
expect_rc "AB-06: nome di un remote: codice 0" 0
expect_no_out "AB-06: nome di un remote: non e' una copia" "is a copy"
expect_ok "AB-06: nome di un remote: ultima versione vista registrata" \
	git config "shared.$ID.authoritySeen"
run git shared link -c nessuno "$(shared_id o.txt 2>/dev/null || echo 0123456789abcdef0123456789abcdef)"
expect_rc "AB-06: parola che non e' un remote: codice 1" 1

# --- AB-09: iscrizione respinta perche' l'autorita' e' piu' avanti ----------------
git init -q --bare -b main "$TRASH/srv/R9.git"
reader R9
git remote add origin "$TRASH/srv/R9.git"
git config shared.heir true
cd "$TRASH/A" || exit 1
edit l3
git shared publish >/dev/null 2>&1
git shared push >/dev/null 2>&1
cd "$TRASH/R9" || exit 1
run git shared push
expect_rc "AB-09: push con l'autorita' piu' avanti: codice 0" 0
expect_err "AB-09: dice di fare pull" "has a newer version: run 'git shared pull'"
expect_no_out "AB-09: non dice 'already on'" "already on the"

# --- S-12, B-SA4: crown --accept di un rifiuto tiene il creatore ------------------
# R5 rifiuta un'autorita' X2 che cambia anche il creatore (C2). Poi la
# versione sparisce e un peer porta X2 con un altro creatore (C3):
# accettare X2 con il creatore C2 non accetta anche C3.
reader R5
T5=$(tip R5)
C2=$(blob "$TRASH/srv/C2.git")
X2=$(blob "$TRASH/srv/X2.git")
V=$(mkver "$T5" "$C2" "$X2" -)
setseen "$V"
run git shared crown --reject p.h
expect_rc "S-12: rifiuto: codice 0" 0
expect_eq "S-12: rifiuto con il creatore" "$(git config "shared.$ID.authorityRejected")" \
	"$(printf '%s\t%s' "$TRASH/srv/X2.git" "$TRASH/srv/C2.git")"
setseen "$T5"
V3=$(mkver "$T5" "$(blob "$TRASH/srv/C3.git")" "$X2" -)
git init -q --bare "$TRASH/P.git"
git push -q "$TRASH/P.git" "$V3:refs/shared/$ID"
git config --add "shared.$ID.peer" "$TRASH/P.git"
run git shared crown --yes --accept "$TRASH/srv/X2.git" p.h
expect_rc "S-12: accettazione del rifiuto: codice 0" 0
expect_eq "S-12: un creatore diverso non e' accettato: T non avanza" "$(tip R5)" "$T5"
run git shared status
expect_out "S-12: il creatore C3 resta da decidere" "the creator changes too, to $TRASH/srv/C3.git"

# --- S-10: ssh senza domande, GCM senza finestre ----------------------------------
mkdir "$TRASH/bin"
cat >"$TRASH/bin/ssh" <<EOF
#!/bin/sh
printf 'args %s\n' "\$*" >>"$TRASH/ssh.log"
printf 'gcm %s\n' "\$GCM_INTERACTIVE" >>"$TRASH/ssh.log"
exit 1
EOF
chmod +x "$TRASH/bin/ssh"
make_repo S10
cd "$TRASH/S10" || exit 1
put q.h q
git shared set -c q.h >/dev/null
IDQ=$(shared_id q.h)
TQ=$(git rev-parse "refs/shared/$IDQ")
TREE=$(printf '100644 blob %s\tcontent\n100644 blob %s\tcreator\n' \
	"$(git rev-parse "$TQ:content")" "$(blob "ssh://gs.invalid/q.git")" | git mktree)
git update-ref "refs/shared/$IDQ" "$(git commit-tree "$TREE" -p "$TQ" -m 'shared: q.h')"
# Il comando che git lancia si legge dal trace (Git for Windows mette la
# sua ssh davanti nel PATH: un finto "ssh" nel PATH non basterebbe).
GIT_TRACE="$TRASH/trace1" git shared status --remote >/dev/null 2>&1
LOG=$(grep "run_command" "$TRASH/trace1" 2>/dev/null)
case "$LOG" in
*"ssh -o BatchMode=yes -o ConnectTimeout=15"*) ok "S-10: ssh con BatchMode e ConnectTimeout" ;;
*) not_ok "S-10: ssh con BatchMode e ConnectTimeout" "trace: $LOG" ;;
esac
# Con un core.sshCommand dell'utente vale il suo; il finto ssh, per path,
# dice anche l'ambiente che riceve.
git config core.sshCommand "'$TRASH/bin/ssh' -o Mine=1"
GIT_TRACE="$TRASH/trace2" git shared status --remote >/dev/null 2>&1
LOG=$(cat "$TRASH/ssh.log" 2>/dev/null)
case "$LOG" in
*Mine=1*) ok "S-10: core.sshCommand dell'utente vale" ;;
*) not_ok "S-10: core.sshCommand dell'utente vale" "log: $LOG" ;;
esac
expect_fail "S-10: con core.sshCommand niente BatchMode nostro" grep -q BatchMode "$TRASH/trace2"
case "$LOG" in
*"gcm never"*) ok "S-10: GCM_INTERACTIVE=never" ;;
*) not_ok "S-10: GCM_INTERACTIVE=never" "log: $LOG" ;;
esac

done_testing
