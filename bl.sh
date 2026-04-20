#!/bin/sh
# Beerlang REPL wrapper
# Resolves lib/core.beer relative to this script's location
# and wraps with rlwrap for line editing if available.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
export BEERPATH="${SCRIPT_DIR}/lib"

BEER="${SCRIPT_DIR}/bin/beerlang"
if [ ! -x "$BEER" ]; then
    BEER="${SCRIPT_DIR}/beerlang"
fi

if [ ! -x "$BEER" ]; then
    echo "Error: beerlang binary not found in ${SCRIPT_DIR}" >&2
    exit 1
fi

COMPLETIONS="${SCRIPT_DIR}/.rlwrap_completions"

# Generate completions file if it doesn't exist
if [ ! -f "$COMPLETIONS" ]; then
    "$BEER" -e "(doseq [sym (ns-publics 'beer.core)] (println sym))" \
        > "$COMPLETIONS" 2>/dev/null || rm -f "$COMPLETIONS"
fi

if command -v rlwrap >/dev/null 2>&1; then
    if [ -f "$COMPLETIONS" ]; then
        exec rlwrap -f "$COMPLETIONS" "$BEER" "$@"
    else
        exec rlwrap "$BEER" "$@"
    fi
else
    exec "$BEER" "$@"
fi
