#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PORT=8080

# Build first
echo "Building..."
source "${SCRIPT_DIR}/../emsdk/emsdk_env.sh" > /dev/null 2>&1
make -C "${SCRIPT_DIR}"

# Kill anything already on the port
fuser -k ${PORT}/tcp 2>/dev/null && echo "Killed existing server on :${PORT}" || true

cd "${SCRIPT_DIR}/build"
echo "Serving at http://localhost:${PORT}/missile_command.html  (cache disabled)"
python3 - ${PORT} <<'EOF'
import sys, http.server

class NoCacheHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-cache, no-store, must-revalidate")
        self.send_header("Pragma", "no-cache")
        self.send_header("Expires", "0")
        super().end_headers()
    def log_message(self, fmt, *args):
        pass  # suppress per-request noise

port = int(sys.argv[1])
http.server.test(HandlerClass=NoCacheHandler, port=port, bind="0.0.0.0")
EOF
