#!/bin/sh
# run.sh - esegue tutti i test t[0-9]*.sh e riassume l'esito.

cd "$(dirname "$0")" || exit 1

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
