#!/bin/sh
# t0022-binari.sh - file binari e scelta di una versione intera con
# update --ours / --theirs (REQ-F-009, REQ-F-011, REQ-NF-010, D-108).

. "$(dirname "$0")/lib.sh"

# bin <file> <testo>: file binario (byte nullo in mezzo)
bin() {
	printf '%s\000%s\n' "$2" "$2" >"$1"
}

# change <testo>: riscrive f.bin e committa
change() {
	bin f.bin "$1"
	git commit -q -a -m "f.bin: $1"
}

# same <file> <testo>: il file e' il binario <testo>
same() {
	bin "$TRASH/.expect" "$2"
	cmp -s "$1" "$TRASH/.expect"
}

# pair <A> <B>: A condivide f.bin, B lo collega
pair() {
	make_repo "$1"
	cd "$TRASH/$1" || exit 1
	bin f.bin v0
	git add f.bin
	git commit -q -m init
	git shared set -c f.bin >/dev/null
	make_repo "$2"
	cd "$TRASH/$2" || exit 1
	put o.txt o
	git shared link -c "../$1/f.bin" >/dev/null
}

state() {
	git shared status 2>/dev/null | grep state
}

tip() {
	git rev-parse "refs/shared/$(shared_id f.bin)$1"
}

has_marker() {
	git rev-parse -q --verify "refs/shared-merge/$(shared_id f.bin)" >/dev/null
}

# --- un binario viaggia come ogni file ------------------------------------------
pair A B
expect_ok "link: il binario arriva intatto" same f.bin v0
cd "$TRASH/A" || exit 1
change a1
git shared publish >/dev/null
cd "$TRASH/B" || exit 1
expect_eq "da applicare" "$(state)" "    state  new version to apply"
run git shared update -c
expect_rc "applicazione: codice 0" 0
expect_out "applicazione: applicato e committato" "f.bin: new version applied and committed"
expect_ok "applicazione: contenuto intatto" same f.bin a1
expect_eq "applicazione: allineato" "$(state)" "    state  aligned"

# --- divergenza del branch: conflitto senza marker -------------------------------
cd "$TRASH/A" || exit 1
change a2
git shared publish >/dev/null
T_A=$(tip)
cd "$TRASH/B" || exit 1
change b2
expect_eq "divergente" "$(state)" "    state  diverged"
run git shared update
expect_rc "binario: conflitto, codice 1" 1
expect_err "binario: segnalato" "f.bin: binary file changed on both sides"
expect_err "binario: come vedere l'altra" "git show ':3:f.bin'"
expect_err "binario: come scegliere" "git shared update --ours' or '--theirs'"
expect_err "binario: come rinunciare" "git shared update --abort 'f.bin'"
expect_eq "binario: conflitto come in git" "$(git status --short f.bin)" "UU f.bin"
expect_ok "binario: nel file la versione nostra, senza marker" same f.bin b2
git show :3:f.bin >"$TRASH/.theirs"
expect_ok "binario: l'altra versione nello stage 3" same "$TRASH/.theirs" a2
expect_eq "binario: unione in corso" "$(state)" "    state  merge in progress"
expect_ok "binario: unione registrata" has_marker
expect_eq "binario: T non cambia" "$(tip)" "$T_A"
run git shared update
expect_rc "binario: update senza scelta rifiuta" 1
expect_err "binario: ricorda --ours e --theirs" "--ours' or '--theirs'"

# --ours: la versione nostra, uguale a HEAD, e' una scelta e non un abbandono.
run git shared update --ours
expect_rc "--ours: codice 0" 0
expect_out "--ours: niente da committare" "f.bin: merged, nothing to commit"
expect_ok "--ours: file nostro" same f.bin b2
expect_eq "--ours: nessun conflitto nell'index" "$(git status --short f.bin)" ""
expect_fail "--ours: unione tolta" has_marker
expect_eq "--ours: versione figlia di T" "$(tip ^)" "$T_A"
git show "$(tip):content" >"$TRASH/.v"
expect_ok "--ours: la versione ha il contenuto nostro" same "$TRASH/.v" b2
expect_eq "--ours: allineato" "$(state)" "    state  aligned"
run git shared publish
expect_out "--ours: inviata" "1 of 1 peers notified"
cd "$TRASH/A" || exit 1
git shared update -c >/dev/null 2>&1
expect_ok "--ours: A applica la scelta" same f.bin b2

# --- --theirs con -c: la versione ricevuta, nessuna versione nuova ---------------
change a3
git shared publish >/dev/null
T_A=$(tip)
cd "$TRASH/B" || exit 1
git shared update >/dev/null 2>&1 # da allineato: la applica
git commit -q -m "a3"
change b3
cd "$TRASH/A" || exit 1
change a4
git shared publish >/dev/null
T_A=$(tip)
cd "$TRASH/B" || exit 1
git shared update >/dev/null 2>&1
expect_ok "--theirs: conflitto aperto" has_marker
run git shared update --theirs -c
expect_rc "--theirs: codice 0" 0
expect_out "--theirs: applicata e committata" "f.bin: new version applied and committed"
expect_ok "--theirs: file loro" same f.bin a4
expect_ok "--theirs: committato" same "$TRASH/B/f.bin" a4
expect_eq "--theirs: nessuna versione nuova" "$(tip)" "$T_A"
expect_fail "--theirs: unione tolta" has_marker
expect_eq "--theirs: allineato" "$(state)" "    state  aligned"

# --- divergenza tra versioni: due genitori ----------------------------------------
pair Q R
cd "$TRASH" && mv R R.off
cd "$TRASH/Q" || exit 1
change q1
git shared publish >/dev/null 2>&1
T_Q=$(tip)
cd "$TRASH" && mv R.off R
cd "$TRASH/R" || exit 1
change r1
git shared publish >/dev/null 2>&1 # notifica Q
T_R=$(tip)
run git shared update --all # l'avviso di Q e' andato perso
expect_rc "tra versioni: conflitto" 1
expect_err "tra versioni: binario" "binary file changed on both sides"
expect_ok "tra versioni: file nostro" same f.bin r1
run git shared update --theirs f.bin
expect_rc "tra versioni --theirs: codice 0" 0
expect_out "tra versioni --theirs: unita" "f.bin: merged. Commit it to confirm"
expect_ok "tra versioni --theirs: file del peer" same f.bin q1
expect_eq "tra versioni: primo genitore la T di partenza" "$(tip ^1)" "$T_R"
expect_eq "tra versioni: secondo genitore la versione del peer" "$(tip ^2)" "$T_Q"
expect_eq "tra versioni: da committare" "$(state)" "    state  applied, to commit"
git commit -q -m "scelta q1"
run git shared publish
expect_out "tra versioni: inviata" "1 of 1 peers notified"
cd "$TRASH/Q" || exit 1
run git shared update -c
expect_out "tra versioni: Q ha gia' il contenuto scelto" "f.bin: up to date"
expect_eq "tra versioni: Q allineato" "$(state)" "    state  aligned"

# --- testo: --ours al posto dei marker ---------------------------------------------
make_repo C
cd "$TRASH/C" || exit 1
printf 'a\nb\nc\n' >f.h
git add f.h
git commit -q -m init
git shared set -c f.h >/dev/null
make_repo D
cd "$TRASH/D" || exit 1
put o.txt o
git shared link -c ../C/f.h >/dev/null
cd "$TRASH/C" || exit 1
printf 'a\nC\nc\n' >f.h
git commit -q -a -m C
git shared publish >/dev/null
T_C=$(git rev-parse "refs/shared/$(shared_id f.h)")
cd "$TRASH/D" || exit 1
printf 'a\nD\nc\n' >f.h
git commit -q -a -m D
run git shared update
expect_rc "testo: conflitto" 1
expect_err "testo: propone --ours e --theirs" "git shared update --ours' or '--theirs'"
expect_ok "testo: marker nel file" grep -q '^<<<<<<< ours' f.h
run git shared update --ours f.h
expect_rc "testo --ours: codice 0" 0
expect_eq "testo --ours: file nostro, senza marker" "$(tr '\n' ' ' <f.h)" "a D c "
expect_eq "testo --ours: versione figlia di T" \
	"$(git rev-parse "refs/shared/$(shared_id f.h)^")" "$T_C"
expect_eq "testo --ours: allineato" "$(git shared status 2>/dev/null | grep state)" \
	"    state  aligned"

# Risoluzione gia' in staging: --theirs non la scavalca.
cd "$TRASH/C" || exit 1
git shared update -c >/dev/null 2>&1
printf 'a\nC2\nc\n' >f.h
git commit -q -a -m C2
git shared publish >/dev/null
cd "$TRASH/D" || exit 1
printf 'a\nD2\nc\n' >f.h
git commit -q -a -m D2
git shared update >/dev/null 2>&1
printf 'a\nCD2\nc\n' >f.h
git add f.h
run git shared update --theirs
expect_rc "in staging: --theirs rifiuta" 1
expect_err "in staging: messaggio" "f.h: the conflict is already resolved in the index"
expect_eq "in staging: risoluzione intatta" "$(git show :f.h | tr '\n' ' ')" "a CD2 c "
run git shared update -c
expect_rc "in staging: update conclude" 0
expect_eq "in staging: contenuto della risoluzione" \
	"$(git show HEAD:f.h | tr '\n' ' ')" "a CD2 c "

# --- errori ---------------------------------------------------------------------------
run git shared update --ours
expect_rc "senza unione: codice 1" 1
expect_err "senza unione: messaggio" "no merge in progress"
run git shared update --theirs f.h
expect_rc "senza unione <file>: codice 1" 1
expect_err "senza unione <file>: messaggio" "f.h: no merge in progress"
run git shared update --ours --theirs
expect_rc "--ours e --theirs: uso" 129
expect_err "--ours e --theirs: messaggio" \
	"use only one of --ours and --theirs, without --abort, -f or --all"
run git shared update --ours --abort
expect_rc "--ours con --abort: uso" 129
run git shared update -f --theirs
expect_rc "--theirs con -f: uso" 129
run git shared update --theirs --all
expect_rc "--theirs con --all: uso" 129
run git shared update -h
expect_out "-h: --ours" "--ours"
expect_out "-h: --theirs" "--theirs"

done_testing
