#!/bin/sh
# run.sh - esegue tutti i test t[0-9]*.sh e riassume l'esito.

cd "$(dirname "$0")" || exit 1

# Versione di git provata (matrice di D-032): quella che il PATH trova,
# es. PATH="$HOME/git-latest/bin:$PATH" make test
echo "# $(git --version) ($(uname -s))"

failed=""
for t in t[0-9]*.sh; do
	echo "# --- $t"
	sh "$t" || failed="$failed $t"
done

if [ -z "$failed" ]; then
	echo "# tutti i test passati"
else
	echo "# falliti:$failed"
	exit 1
fi
