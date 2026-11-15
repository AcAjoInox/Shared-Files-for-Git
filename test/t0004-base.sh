#!/bin/sh
# t0004-base.sh - base e stato con catene di piu' versioni (D-054, D-065).
#
# Le versioni successive alla prima sono costruite a mano con il plumbing,
# come le creera' la pubblicazione (M2).

. "$(dirname "$0")/lib.sh"

# version <contenuto> [<genitore>]: commit di versione con quel contenuto
version() {
	b=$(printf '%s\n' "$1" | git hash-object -w --stdin)
	t=$(printf '100644 blob %s\tcontent\n' "$b" | git mktree)
	if [ -n "$2" ]; then
		git commit-tree "$t" -p "$2" -m "shared: f.h"
	else
		git commit-tree "$t" -m "shared: f.h"
	fi
}

make_repo s
cd "$TRASH/s" || exit 1
put f.h A
git shared set -c f.h >/dev/null
C_SET=$(git rev-parse HEAD) # storia di f.h: solo A
ID=$(shared_id f.h)
V1=$(git rev-parse "refs/shared/$ID")

# catena v1(A) v2(B); branch fermo ad A
V2=$(version B "$V1")
git update-ref "refs/shared/$ID" "$V2"
run git shared status
expect_out "storia A, catena A-B: da applicare" "new version to apply"

# storia A, B: allineato
put f.h B
C_B=$(git rev-parse HEAD)
run git shared status
expect_out "storia A-B, catena A-B: allineato" "aligned"

# storia A, B, A: revert non pubblicato, base = T (B-01)
put f.h A
run git shared status
expect_out "revert (storia A-B-A): da pubblicare" "to publish"

# storia A, B, C: modifica su T
git reset -q --hard "$C_B"
put f.h C
run git shared status
expect_out "storia A-B-C: da pubblicare" "to publish"

# catena v1(A) v2(B) v3(A): il revert e' stato pubblicato
V3=$(version A "$V2")
git update-ref "refs/shared/$ID" "$V3"
git reset -q --hard "$C_B"
put f.h A
run git shared status
expect_out "storia A-B-A, catena A-B-A: allineato" "aligned"

# storia A, B, D con catena A-B-A: base v2, non v3 (B-14)
git reset -q --hard "$C_B"
put f.h D
run git shared status
expect_out "storia A-B-D, catena A-B-A: divergente" "diverged"

# storia A, B con catena A-B-A: base v2, H = B = blob di v2
git reset -q --hard "$C_B"
run git shared status
expect_out "storia A-B, catena A-B-A: da applicare" "new version to apply"

# storia A sola con catena A-B-A: H = A = T
git reset -q --hard "$C_SET"
run git shared status
expect_out "storia A, catena A-B-A: allineato (H = T)" "aligned"

done_testing
