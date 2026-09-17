#!/bin/sh
# sync-unicode-tables.sh - regenerate src/unicode_tables.h from coffer.
#
# coffer's gen_unicode_tables.py is the source of truth for the UAX #11
# (East Asian Width) and UAX #29 (grapheme cluster) interval tables.
# boba vendors its output as a data-only header so both projects measure
# text with one Unicode version, without boba linking coffer.
#
# Point COFFER at a coffer checkout (default: ../coffer):
#   COFFER=~/src/coffer scripts/sync-unicode-tables.sh
#
# The generator writes a provenance banner (UCD version + per-file
# SHA-256 + the generator's own SHA-256 + the command line). A stale
# vendored copy shows up as a version/sha mismatch there. This script
# only rewrites the banner's "Regenerate:" line to name itself - the
# entry point a boba reader should use. Rerun, then commit the diff.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
coffer=${COFFER:-$root/../coffer}
gen="$coffer/src/scripts/gen_unicode_tables.py"
out="$root/src/unicode_tables.h"

if [ ! -f "$gen" ]; then
	echo "sync-unicode-tables: no generator at $gen" >&2
	echo "  set COFFER to a coffer checkout" >&2
	exit 1
fi

tmp=$(mktemp)
final=$(mktemp)
trap 'rm -f "$tmp" "$final"' EXIT

python3 "$gen" --prefix TU_ --type TuiRange --out "$tmp"
sed 's#^ \*   python3 .*--out .*# *   scripts/sync-unicode-tables.sh   (needs a coffer checkout: $COFFER)#' \
	"$tmp" >"$final"

if [ -f "$out" ] && cmp -s "$final" "$out"; then
	echo "sync-unicode-tables: src/unicode_tables.h is up to date"
	exit 0
fi

mv "$final" "$out"
trap 'rm -f "$tmp"' EXIT
echo "sync-unicode-tables: wrote src/unicode_tables.h"
