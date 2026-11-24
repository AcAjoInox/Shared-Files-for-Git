#!/bin/sh
# run.sh - esegue tutti i test t[0-9]*.sh, JOBS alla volta, e riassume
# l'esito.
#
#   sh test/run.sh              JOBS = meta' dei processori logici
#   JOBS=1 sh test/run.sh       uno alla volta, come prima
#   make test JOBS=4
#
# I file sono indipendenti (ognuno nella sua cartella temporanea, lib.sh):
# girano in processi separati, il sistema li distribuisce sui core. Il
# limite e' la creazione dei processi, non i core: su Windows con 12
# processori logici 6 alla volta finiscono in 217 s, 12 alla volta in
# 404 s (sequenziale: 1167 s). Da qui il default a meta'.
#
# Ogni file scrive in un log suo; alla fine i log si stampano nell'ordine
# dei nomi, come in sequenza. Durante l'esecuzione una riga per file
# finito. Partono prima i file piu' lunghi, per non lasciarne uno da solo
# alla fine.

cd "$(dirname "$0")" || exit 1

if [ -z "$JOBS" ]; then
	cpus=$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null)
	JOBS=$(( ${cpus:-2} / 2 ))
fi
case "$JOBS" in
'' | *[!0-9]*) echo "JOBS non valido: $JOBS"; exit 1 ;;
esac
[ "$JOBS" -ge 1 ] || JOBS=1

LOGS=$(mktemp -d "${TMPDIR:-/tmp}/git-shared-run.XXXXXX") || exit 1
trap 'rm -rf "$LOGS"' EXIT

# Versione di git provata (matrice di D-032): quella che il PATH trova,
# es. PATH="$HOME/git-latest/bin:$PATH" make test
echo "# $(git --version) ($(uname -s)), $JOBS alla volta"
start=$(date +%s)

# Piu' righe prima. Ogni file: log, codice di uscita, riga di avanzamento.
for t in t[0-9]*.sh; do
	echo "$(wc -l <"$t") $t"
done | sort -rn | awk '{ print $2 }' |
	xargs -P "$JOBS" -n 1 sh -c '
		s=$(date +%s)
		sh "$1" >"$0/$1.log" 2>&1
		rc=$?
		echo $rc >"$0/$1.rc"
		[ $rc -eq 0 ] && esito=ok || esito=FALLITO
		echo "# $esito $1 ($(( $(date +%s) - s )) s)"' "$LOGS"

failed=""
for t in t[0-9]*.sh; do
	echo "# --- $t"
	cat "$LOGS/$t.log" 2>/dev/null
	[ "$(cat "$LOGS/$t.rc" 2>/dev/null)" = 0 ] || failed="$failed $t"
done

echo "# $(( $(date +%s) - start )) s"
if [ -z "$failed" ]; then
	echo "# tutti i test passati"
else
	echo "# falliti:$failed"
	exit 1
fi
