#!/bin/sh
# t0024-merge-versioni.sh - unioni tra versioni pubblicate da peer diversi
# (REQ-F-011, D-078, D-091, D-092; reportIdea B-46, B-47, B-49).

. "$(dirname "$0")/lib.sh"
. "$TEST_DIR/lib-merge.sh"

# --- due peer pubblicano senza vedersi: versione con due genitori ----------------
pair E F
cd "$TRASH" || exit 1
mv F F.off
cd "$TRASH/E" || exit 1
change E1 b c d e
git shared publish >/dev/null 2>&1
T_E=$(tip)
cd "$TRASH" || exit 1
mv F.off F
cd "$TRASH/F" || exit 1
change a b c d F5
run git shared publish
expect_rc "peer: publish accettato, E notificato" 0
T_F=$(tip)
run git shared update --all # l'avviso di E e' andato perso
expect_rc "peer: codice 0" 0
expect_out "peer: unite" "f.h: merged. Commit it to confirm"
expect_eq "peer: contenuto" "$(tr '\n' ' ' <f.h)" "E1 b c d F5 "
expect_eq "peer: primo genitore la T locale" "$(tip ^1)" "$T_F"
expect_eq "peer: secondo genitore la versione del peer" "$(tip ^2)" "$T_E"
git commit -q -m "merge f.h"
run git shared publish
expect_rc "peer: publish accettato" 0
cd "$TRASH/E" || exit 1
run git shared update -c
expect_rc "peer: E applica" 0
expect_eq "peer: E ha il contenuto unito" "$(tr '\n' ' ' <f.h)" "E1 b c d F5 "
expect_eq "peer: E allineato" "$(state)" "    state  aligned"

# --- il peer la cui versione e' stata unita riceve l'unione (B-46) ---------------
# F ha unito la versione T_E di E: la versione M ha T_E come secondo
# genitore. F toglie la riga di E e pubblica V2. E, che ha nel branch il
# contenuto di T_E, deve risultare da applicare (base T_E), non divergente
# con base la prima versione, e il suo update non deve rimettere la riga.
cd "$TRASH/F" || exit 1
expect_eq "B-46: F allineato dopo l'unione" "$(state)" "    state  aligned"
change a b c d F5
run git shared publish
expect_out "B-46: F pubblica la rimozione" "1 of 1 peers notified"
T_F2=$(tip)
cd "$TRASH/E" || exit 1
git reset -q --hard HEAD~1 # E torna al commit della sua versione T_E
git shared status >/dev/null 2>&1 # E riceve (D-099)
expect_eq "B-46: H di E e' la versione T_E" "$(git rev-parse HEAD:f.h)" \
	"$(git rev-parse "$T_E:content")"
expect_eq "B-46: T di E e' la versione di F" "$(tip)" "$T_F2"
expect_eq "B-46: da applicare, non divergente" "$(state)" \
	"    state  new version to apply"
run git shared publish
expect_no_out "B-46: publish non invia nulla" "peers notified"
run git shared update -c
expect_rc "B-46: update codice 0" 0
expect_out "B-46: applicata, non unita" "new version applied and committed"
expect_eq "B-46: la riga tolta da F resta tolta" \
	"$(git show HEAD:f.h | tr '\n' ' ')" "a b c d F5 "
expect_eq "B-46: T non cambia" "$(tip)" "$T_F2"
expect_eq "B-46: allineato" "$(state)" "    state  aligned"

# La versione T_E in staging, sopra un HEAD piu' vecchio, non e' lavoro
# dell'utente: e' una versione della catena (D-076), update la sostituisce.
git reset -q --hard HEAD~2
git show "$T_E:content" >f.h
git add f.h
run git shared update -c
expect_rc "B-46: versione unita in staging non e' modifica locale" 0
expect_eq "B-46: applicata l'ultima versione" \
	"$(git show HEAD:f.h | tr '\n' ' ')" "a b c d F5 "

# --- update: T avanza solo su una versione che la contiene (B-47) ---------------
# L ha i peer [M, N], in quest'ordine. N ha vN, che discende da vL; M ha vM,
# nata dalla prima versione. merge-base --independent restituisce vM e vN
# nell'ordine dei peer: T deve avanzare su vN, l'unica che contiene vL, e
# vM si unisce dopo, con due genitori.
pair L M
make_repo N
cd "$TRASH/N" || exit 1
put o.txt o
git shared link -c ../L/f.h >/dev/null
cd "$TRASH" && mv M M.off
cd "$TRASH/L" || exit 1
change L1 b c d e
git shared publish >/dev/null 2>&1 # avviso solo a N
git -C "$TRASH/N" shared status >/dev/null 2>&1 # N riceve prima che L sparisca
T_L=$(tip)
cd "$TRASH" && mv L L.off
cd "$TRASH/N" || exit 1
git shared update -c >/dev/null 2>&1
change L1 b N3 d e
git shared publish >/dev/null 2>&1 # nessun peer raggiungibile
T_N=$(tip)
cd "$TRASH" && mv L.off L && mv M.off M
cd "$TRASH/M" || exit 1
change a b c d M5
git shared publish >/dev/null 2>&1 # notifica L e N
T_M=$(tip)
cd "$TRASH/L" || exit 1
expect_eq "B-47: peer di L nell'ordine M, N" \
	"$(git config --get-all "shared.$(shared_id f.h).peer" | sed 's|.*/||' | tr '\n' ' ')" \
	"M N "
run git shared update --all # vN senza avviso: L era spostata
expect_rc "B-47: codice 0" 0
expect_out "B-47: versione ricevuta" "new version received"
expect_ok "B-47: la T di prima e' antenata della nuova" \
	git merge-base --is-ancestor "$T_L" "$(tip)"
expect_eq "B-47: primo genitore la versione che contiene T" "$(tip ^1)" "$T_N"
expect_eq "B-47: secondo genitore l'altra versione" "$(tip ^2)" "$T_M"
expect_eq "B-47: contenuto unito" "$(tr '\n' ' ' <f.h)" "L1 b N3 d M5 "

# --- conclusione di un conflitto tra versioni: una sola versione (B-49) -----------
# La risoluzione diventa una versione con genitori (T di partenza, versione
# del peer); la versione del peer, gia' contenuta, non si unisce di nuovo.
pair Q R
cd "$TRASH" && mv R R.off
cd "$TRASH/Q" || exit 1
change Q1 b c d e
git shared publish >/dev/null 2>&1
T_Q=$(tip)
cd "$TRASH" && mv R.off R
cd "$TRASH/R" || exit 1
change R1 b c d e
git shared publish >/dev/null 2>&1 # notifica Q
T_R=$(tip)
run git shared update --all # l'avviso di Q e' andato perso
expect_rc "B-49: conflitto" 1
lines f.h QR1 b c d e
git add f.h
run git shared update -c
expect_rc "B-49: conclusa" 0
expect_out "B-49: unita e committata" "f.h: merged and committed"
expect_eq "B-49: primo genitore la T di partenza" "$(tip ^1)" "$T_R"
expect_eq "B-49: secondo genitore la versione del peer" "$(tip ^2)" "$T_Q"
expect_eq "B-49: contenuto della risoluzione" "$(git show HEAD:f.h | tr '\n' ' ')" \
	"QR1 b c d e "
expect_eq "B-49: allineato" "$(state)" "    state  aligned"
run git shared publish
expect_out "B-49: inviata" "1 of 1 peers notified"
cd "$TRASH/Q" || exit 1
run git shared update -c
expect_out "B-49: Q applica" "new version applied and committed"
expect_eq "B-49: Q allineato" "$(state)" "    state  aligned"

done_testing
