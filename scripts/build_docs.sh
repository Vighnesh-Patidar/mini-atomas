#!/usr/bin/env bash
# Build the Doxygen API documentation. Output lands in docs/api/html/.
#
# Usage:
#   scripts/build_docs.sh
#
# Requires: doxygen, graphviz (for the call-graph SVGs — set HAVE_DOT=NO
# in Doxyfile if you don't have graphviz).

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if ! command -v doxygen >/dev/null 2>&1; then
    echo "doxygen not found. Install:" >&2
    echo "  Debian/Kali: sudo apt-get install doxygen graphviz" >&2
    echo "  macOS:       brew install doxygen graphviz" >&2
    exit 1
fi

# Wipe stale output so removed symbols don't linger.
rm -rf docs/api/html

echo "Running Doxygen..." >&2
doxygen Doxyfile 2>&1 | tee /tmp/doxygen-build.log | tail -5

echo
echo "Done." >&2
echo "Open docs/api/html/index.html in a browser." >&2
echo "Warnings (if any) recorded in /tmp/doxygen-build.log." >&2
