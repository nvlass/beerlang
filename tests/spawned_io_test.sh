#!/bin/bash
# Minimal reproducer for spawned-task IO-blocking bug.
#
# Root cause: when tcp/accept is called inside a spawned task and blocks
# (EAGAIN), the IO reactor never wakes that task. Works fine when tcp/accept
# is called from the inline REPL task.
#
# Run: bash tests/spawned_io_test.sh

BEER="${BEER:-./bin/beerlang}"
PASS=0
FAIL=0

if [ ! -x "$BEER" ]; then
    echo "ERROR: $BEER not found. Run 'make' first."
    exit 1
fi

run_repl() {
    # $1 = beer expression(s), one per line
    printf '%s\n(exit)\n' "$1" | "$BEER" 2>/dev/null \
        | sed -n 's/^[a-zA-Z._]*:[0-9]*> //p' \
        | grep -v '^$\|^Goodbye' \
        | tail -1
}

echo "=== Spawned-task IO blocking bug reproducer ==="
echo ""

# -----------------------------------------------------------------------
# CASE 1: inline REPL task does tcp/accept (should work)
# -----------------------------------------------------------------------
echo "--- CASE 1: inline tcp/accept in REPL task ---"
PORT=19901

# Background client: wait 2s then connect and send 5 bytes
(sleep 2 && printf '(require (quote beer.tcp) :as (quote tcp))\n(let [c (tcp/connect "127.0.0.1" '$PORT')] (write c "hello") (flush c))\n(exit)\n' | "$BEER" >/dev/null 2>&1) &
CLIENT_PID=$!

# Server (inline): listen, accept in the REPL task itself, read 5 bytes
RESULT=$(run_repl '(require (quote beer.tcp) :as (quote tcp))
(let [srv (tcp/listen '$PORT') cli (tcp/accept srv) _ (read-bytes cli 5)] (println "accepted"))')
wait $CLIENT_PID 2>/dev/null || true

if [ "$RESULT" = '"accepted"' ] || [ "$RESULT" = "accepted" ]; then
    echo "PASS: inline tcp/accept wakes on client connect"
    PASS=$((PASS+1))
else
    echo "FAIL: inline tcp/accept => '$RESULT' (expected 'accepted')"
    FAIL=$((FAIL+1))
fi

sleep 0.3

# -----------------------------------------------------------------------
# CASE 2: spawned task does tcp/accept (the broken case)
# -----------------------------------------------------------------------
echo ""
echo "--- CASE 2: tcp/accept inside (spawn ...) ---"
PORT=19920

# Background client: wait 1s then connect and send 5 bytes
(sleep 1 && printf '(require (quote beer.tcp) :as (quote tcp))\n(let [c (tcp/connect "127.0.0.1" '$PORT')] (write c "hello") (flush c))\n(exit)\n' | "$BEER" >/dev/null 2>&1) &
CLIENT_PID=$!

# Server: spawn a task that does tcp/accept, REPL task waits on a channel for the result
# Times out after 5s if the spawned task never wakes
RESULT_FILE=$(mktemp /tmp/beer_spawn_XXXXXX)
printf '(require (quote beer.tcp) :as (quote tcp))\n(let [srv (tcp/listen '$PORT') done (chan 1)] (spawn (fn [] (let [cli (tcp/accept srv)] (read-bytes cli 5) (>! done "ok")))) (<! done))\n(exit)\n' \
    | "$BEER" 2>/dev/null \
    | sed -n 's/^[a-zA-Z._]*:[0-9]*> //p' \
    | grep -v '^$\|^Goodbye' \
    | tail -1 > "$RESULT_FILE" &
BEER_PID=$!
sleep 5
kill $BEER_PID 2>/dev/null || true
wait $BEER_PID 2>/dev/null || true
RESULT=$(cat "$RESULT_FILE")
rm -f "$RESULT_FILE"
wait $CLIENT_PID 2>/dev/null || true

if [ "$RESULT" = '"ok"' ] || [ "$RESULT" = "ok" ]; then
    echo "PASS: spawned tcp/accept wakes on client connect"
    PASS=$((PASS+1))
else
    echo "FAIL: spawned tcp/accept => '$RESULT' (expected 'ok') — spawned task never woke from accept"
    FAIL=$((FAIL+1))
fi

echo ""
echo "=============================="
echo "Passed: $PASS  Failed: $FAIL"
echo "=============================="
[ "$FAIL" -eq 0 ] && echo "All passed!" || exit 1
