#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# deploy.sh — build Missile Command WASM and assemble a deployable dist/
#
# Usage:
#   ./deploy.sh                     # build + write dist/
#   REMOTE=user@host:/var/www/html/missilecommand ./deploy.sh   # + rsync to server
#
# Output files in dist/:
#   index.php              ← renamed missile_command.html (PHP-processable)
#   missile_command.js     ← Emscripten JS glue
#   missile_command.wasm   ← WebAssembly binary
#   missile_command.data   ← preloaded assets (images + sounds)
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD="$SCRIPT_DIR/build"
DIST="$SCRIPT_DIR/dist"
EMSDK_ENV="$SCRIPT_DIR/../emsdk/emsdk_env.sh"

# ── 1. Activate Emscripten SDK ───────────────────────────────────────────────
if [[ -f "$EMSDK_ENV" ]]; then
    # shellcheck disable=SC1090
    source "$EMSDK_ENV"
else
    echo "ERROR: emsdk not found at $EMSDK_ENV" >&2
    echo "       Run:  git clone https://github.com/emscripten-core/emsdk ../emsdk" >&2
    exit 1
fi

# ── 2. Build ─────────────────────────────────────────────────────────────────
cd "$SCRIPT_DIR"
echo "==> Building..."
make
echo ""

# ── 3. Assemble dist/ ────────────────────────────────────────────────────────
echo "==> Assembling dist/..."
rm -rf "$DIST"
mkdir -p "$DIST"

cp "$BUILD/missile_command.js"   "$DIST/"
cp "$BUILD/missile_command.wasm" "$DIST/"
cp "$BUILD/missile_command.data" "$DIST/"

# Build index.php from the readable shell.html template by substituting
# Emscripten's {{{ SCRIPT }}} placeholder with the actual <script> tag.
# This preserves all whitespace and formatting from shell.html.
sed 's|{{{ SCRIPT }}}|<script async src="missile_command.js"></script>|' \
    "$SCRIPT_DIR/shell.html" > "$DIST/index.php"

echo ""
echo "  dist/ ready:"
ls -lh "$DIST"

# ── 4. Optional: rsync to remote server ──────────────────────────────────────
if [[ -n "${REMOTE:-}" ]]; then
    echo ""
    echo "==> Deploying to $REMOTE ..."
    rsync -avz --delete \
        --include="index.php" \
        --include="missile_command.js" \
        --include="missile_command.wasm" \
        --include="missile_command.data" \
        --exclude="*" \
        "$DIST/" "$REMOTE/"
    echo "  Done."
else
    echo ""
    echo "  To upload to your server:"
    echo "    rsync -avz dist/ user@yourserver:/var/www/html/missilecommand/"
    echo "  Or set REMOTE before running:"
    echo "    REMOTE=user@yourserver:/var/www/html/missilecommand ./deploy.sh"
fi

echo ""
echo "  nginx config snippet: wasm_host/nginx.conf"
