#!/bin/sh
# Beerlang REPL wrapper
# Resolves lib/core.beer relative to this script's location
# and wraps with rlwrap for line editing if available.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
export BEER_LIB_PATH="${SCRIPT_DIR}/lib"

BEER="${SCRIPT_DIR}/bin/beerlang"
if [ ! -x "$BEER" ]; then
    BEER="${SCRIPT_DIR}/beerlang"
fi

if [ ! -x "$BEER" ]; then
    echo "Error: beerlang binary not found in ${SCRIPT_DIR}" >&2
    exit 1
fi

if command -v rlwrap >/dev/null 2>&1; then
    exec rlwrap "$BEER" "$@"
else
    exec "$BEER" "$@"
fi
