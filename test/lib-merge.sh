# lib-merge.sh - funzioni comuni dei test di unione (t0009, t0024, t0025).
#
# Uso, dopo lib.sh:   . "$TEST_DIR/lib-merge.sh"

# lines <file> <r1> ... : una riga per argomento
lines() {
	f=$1
	shift
	printf '%s\n' "$@" >"$f"
}

# pair <A> <B>: A condivide f.h (5 righe), B lo collega
pair() {
	make_repo "$1"
	cd "$TRASH/$1" || exit 1
	lines f.h a b c d e
	git add f.h
	git commit -q -m init
	git shared set -c f.h >/dev/null
	make_repo "$2"
	cd "$TRASH/$2" || exit 1
	put o.txt o
	git shared link -c "../$1/f.h" >/dev/null
}

# change <r1> ... : riscrive f.h e committa
change() {
	lines f.h "$@"
	git commit -q -a -m "f.h: $*"
}

state() {
	git shared status 2>/dev/null | grep state
}

tip() {
	git rev-parse "refs/shared/$(shared_id f.h)$1"
}

has_marker() {
	git rev-parse -q --verify "refs/shared-merge/$(shared_id f.h)" >/dev/null
}
