#!/bin/bash
# Rebase a Phoebus BSP overlay from one kernel version onto another.
#
# The overlay is a set of whole files, not a patch, so it cannot just be copied
# onto a newer kernel: files the port changed may also have changed upstream.
# This classifies every overlay file against the OLD pristine kernel and handles
# each class correctly:
#
#   vendor-only  file does not exist in the old pristine tree -> copy verbatim
#                (upstream cannot have changed a file it does not have)
#   unmoved      exists upstream and is byte-identical old vs new -> copy verbatim
#   moved        exists upstream and changed -> 3-way merge
#                  base   = old pristine (what the port was cut against)
#                  ours   = new pristine (where upstream went)
#                  theirs = the overlay file (what the port did)
#   deleted      gone in the new kernel -> reported, needs a human
#
# Conflicts are left in the output tree with diff3 markers and listed on stderr.
#
# Usage:
#   tools/rebase-overlay.sh OLD_PRISTINE NEW_PRISTINE OLD_OVERLAY OUT_OVERLAY
#
# Example (6.18 -> 7.1):
#   tools/rebase-overlay.sh \
#       build/linux-6.18.39 build/linux-7.1.5 \
#       ../PhoebusBSP-6/overlay ./overlay
set -e

OLD="${1:?old pristine kernel tree}"
NEW="${2:?new pristine kernel tree}"
SRC="${3:?overlay to rebase}"
OUT="${4:?output overlay}"

for d in "$OLD" "$NEW" "$SRC"; do
	[ -d "$d" ] || { echo "ERROR: not a directory: $d" >&2; exit 1; }
done

mkdir -p "$OUT"
conflicts=0
n_vendor=0 n_unmoved=0 n_merged=0 n_deleted=0

while IFS= read -r f; do
	mkdir -p "$OUT/$(dirname "$f")"

	if [ ! -f "$OLD/$f" ]; then
		cp -a "$SRC/$f" "$OUT/$f"; n_vendor=$((n_vendor+1)); continue
	fi
	if [ ! -f "$NEW/$f" ]; then
		echo "DELETED UPSTREAM (needs a human): $f" >&2
		cp -a "$SRC/$f" "$OUT/$f"; n_deleted=$((n_deleted+1)); continue
	fi
	if cmp -s "$OLD/$f" "$NEW/$f"; then
		cp -a "$SRC/$f" "$OUT/$f"; n_unmoved=$((n_unmoved+1)); continue
	fi

	# upstream moved under us -> 3-way merge
	if git merge-file -p --diff3 \
		-L "upstream-new" -L "upstream-base" -L "phoebus-port" \
		"$NEW/$f" "$OLD/$f" "$SRC/$f" > "$OUT/$f" 2>/dev/null; then
		:
	else
		echo "CONFLICT: $f" >&2
		conflicts=$((conflicts+1))
	fi
	n_merged=$((n_merged+1))
done < <(cd "$SRC" && find . -type f | sed 's|^\./||' | sort)

echo
echo "vendor-only (copied):   $n_vendor"
echo "unmoved     (copied):   $n_unmoved"
echo "merged:                 $n_merged  (conflicts: $conflicts)"
echo "deleted upstream:       $n_deleted"
echo
if [ "$conflicts" -gt 0 ] || [ "$n_deleted" -gt 0 ]; then
	echo "Resolve the above by hand, then drop the diff3 markers." >&2
	exit 2
fi
