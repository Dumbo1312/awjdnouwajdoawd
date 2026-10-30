#!/usr/bin/env bash
# fork.sh — build an ArceusCode checkout from a clean OpenCursor clone.
#
# Usage:
#   ./fork.sh [TARGET_DIR]
#
# Default TARGET_DIR = ../arceuscode-src (sibling of this repo).
# This script does NOT build; it just produces the source tree. Run:
#   cd <TARGET_DIR> && npm install && npm run compile
# afterward (and ./scripts/code.sh to launch).
set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"      # arceuscode/
ROOT="$(cd "$HERE/.." && pwd)"                # repo root
TARGET="${1:-$ROOT/../arceuscode-src}"
UPSTREAM="https://github.com/noah-thing/open-cursor.git"
REF="${ARCEUSCODE_UPSTREAM_REF:-main}"

echo "→ ArceusCode fork"
echo "   target : $TARGET"
echo "   upstream: $UPSTREAM@$REF"

if [[ -e "$TARGET" ]]; then
  echo "✗ $TARGET already exists. Delete it or pass another path." >&2
  exit 1
fi

echo "→ cloning OpenCursor ($REF, shallow)…"
GIT_LFS_SKIP_SMUDGE=1 git clone --depth 1 --branch "$REF" "$UPSTREAM" "$TARGET"

echo "→ overlaying product.json"
cp "$HERE/patches/product.json" "$TARGET/product.json"

echo "→ overlaying extensions"
mkdir -p "$TARGET/extensions"
cp -R "$HERE/overlay/extensions/theme-arceuscode"     "$TARGET/extensions/theme-arceuscode"
cp -R "$HERE/overlay/extensions/arceuscode-presets"   "$TARGET/extensions/arceuscode-presets"

echo "→ overlaying branding resources"
if [[ -d "$HERE/overlay/resources/branding" ]]; then
  mkdir -p "$TARGET/resources/arceuscode"
  cp -R "$HERE/overlay/resources/branding/." "$TARGET/resources/arceuscode/"
fi

if [[ -f "$TARGET/README.md" ]]; then
  mv "$TARGET/README.md" "$TARGET/README.OPENCURSOR.md"
fi
cp "$HERE/README.md" "$TARGET/README.md"

echo "→ rewriting user-visible OpenCursor strings in product.json copy (done)."
echo
echo "✓ Source tree ready at: $TARGET"
echo
echo "Next:"
echo "  cd \"$TARGET\""
echo "  npm install                 # ~5 min, pulls Electron etc."
echo "  (cd extensions/arceuscode-presets && npm install && npm run build)"
echo "  npm run compile             # ~5-15 min"
echo "  ./scripts/code.sh           # Linux/macOS, launches ArceusCode"
echo "  # or on Windows:  .\\scripts\\code.bat"
echo
echo "To package a real installer, see upstream's build/gulpfile.vscode.js."
