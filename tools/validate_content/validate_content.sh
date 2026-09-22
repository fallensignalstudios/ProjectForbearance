#!/usr/bin/env bash
# Catalog validation and the numerical preflight, for use in a pre-commit hook or
# a continuous integration job.
#
#   tools/validate_content/validate_content.sh [content_dir] [expansion_binary]
#
# Exits non-zero when the catalog fails to load, when a reference is missing, or
# when the neutral one-day calibration of Section 9.3 no longer reproduces.
set -euo pipefail

content="${1:-content}"
binary="${2:-build/dev/expansion}"

if [[ ! -x "$binary" ]]; then
  echo "validate_content: no expansion binary at '$binary'." >&2
  echo "Build one first:  cmake -S . -B build/dev && cmake --build build/dev" >&2
  exit 2
fi

echo "== catalog validation and neutral preflight"
"$binary" validate --content="$content"

echo
echo "== recorded runs"
shopt -s nullglob
for journal in tests/replays/*.journal.json; do
  echo "-- $(basename "$journal")"
  "$binary" replay --content="$content" --journal="$journal"
done

echo
echo "Reminder: this checks catalog references, the neutral arithmetic and the"
echo "recorded day hashes. It is not a balance, playtest or performance result."
