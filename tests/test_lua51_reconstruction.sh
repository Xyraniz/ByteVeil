#!/usr/bin/env bash
set -euo pipefail
BIN="${1:-./build/byteveil}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"
if ! byteveil_find_lua51; then
    printf 'Lua 5.1 reconstruction tests: SKIP (Lua 5.1 interpreter/compiler not installed)\n'
    exit 0
fi
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cat >"$TMP/control.lua" <<'LUA'
local function classify(value)
    if value < 0 then return "negative"
    elseif value == 0 then return "zero"
    else return "positive" end
end
local total = 0
for i = 1, 3 do total = total + i end
local n = 3
while n > 0 do n = n - 1 end
repeat total = total - 1 until total <= 0
return classify(total)
LUA
"$BYTEVEIL_LUAC51" -o "$TMP/control.luac" "$TMP/control.lua"
"$BIN" --bytecode "$TMP/control.luac" --format lua > "$TMP/reconstructed.lua"
ORIGINAL_PATH="$TMP/control.lua"
RECONSTRUCTED_PATH="$TMP/reconstructed.lua"
if command -v cygpath >/dev/null 2>&1; then
    ORIGINAL_PATH="$(cygpath -m "$ORIGINAL_PATH")"
    RECONSTRUCTED_PATH="$(cygpath -m "$RECONSTRUCTED_PATH")"
fi
"$BYTEVEIL_LUA51" -e "local original=assert(loadfile('$ORIGINAL_PATH')); local reconstructed=assert(loadfile('$RECONSTRUCTED_PATH')); for _, value in ipairs({-1, 0, 1, 3}) do assert(original(value) == reconstructed(value), 'behavior differs for ' .. tostring(value)) end"
grep -q '^for ' "$TMP/reconstructed.lua"
grep -q '^while ' "$TMP/reconstructed.lua"
grep -q '^repeat$' "$TMP/reconstructed.lua"
grep -q 'if ' "$TMP/reconstructed.lua"
grep -q 'elseif\|else' "$TMP/reconstructed.lua"
! grep -q 'unsupported opcode retained' "$TMP/reconstructed.lua"
cat >"$TMP/numeric-closures.lua" <<'LUA'
local functions = {}
for index = 1, 3 do
    functions[index] = function(amount)
        index = index + amount
        return index
    end
end
return functions
LUA
cat >"$TMP/generic-closures.lua" <<'LUA'
local functions = {}
for key, value in ipairs({ "alpha", "beta", "gamma" }) do
    functions[key] = function(increment, suffix)
        key = key + increment
        value = value .. suffix
        return key, value
    end
end
return functions
LUA
"$BYTEVEIL_LUAC51" -o "$TMP/numeric-closures.luac" "$TMP/numeric-closures.lua"
"$BYTEVEIL_LUAC51" -o "$TMP/generic-closures.luac" "$TMP/generic-closures.lua"
"$BIN" --bytecode "$TMP/numeric-closures.luac" --format lua > "$TMP/numeric-closures.reconstructed.lua"
"$BIN" --bytecode "$TMP/generic-closures.luac" --format lua > "$TMP/generic-closures.reconstructed.lua"
if grep -q 'PC dispatcher' "$TMP/numeric-closures.reconstructed.lua"; then
    echo "numeric for with a captured loop variable was not reconstructed structurally" >&2
    exit 1
fi
if grep -q 'PC dispatcher' "$TMP/generic-closures.reconstructed.lua"; then
    echo "generic for with captured loop variables was not reconstructed structurally" >&2
    exit 1
fi
NUMERIC_ORIGINAL_PATH="$TMP/numeric-closures.lua"
NUMERIC_RECONSTRUCTED_PATH="$TMP/numeric-closures.reconstructed.lua"
GENERIC_ORIGINAL_PATH="$TMP/generic-closures.lua"
GENERIC_RECONSTRUCTED_PATH="$TMP/generic-closures.reconstructed.lua"
if command -v cygpath >/dev/null 2>&1; then
    NUMERIC_ORIGINAL_PATH="$(cygpath -m "$NUMERIC_ORIGINAL_PATH")"
    NUMERIC_RECONSTRUCTED_PATH="$(cygpath -m "$NUMERIC_RECONSTRUCTED_PATH")"
    GENERIC_ORIGINAL_PATH="$(cygpath -m "$GENERIC_ORIGINAL_PATH")"
    GENERIC_RECONSTRUCTED_PATH="$(cygpath -m "$GENERIC_RECONSTRUCTED_PATH")"
fi
"$BYTEVEIL_LUA51" "$ROOT/tests/lua51_loop_closure_runtime.lua" "$NUMERIC_ORIGINAL_PATH" "$NUMERIC_RECONSTRUCTED_PATH" "$GENERIC_ORIGINAL_PATH" "$GENERIC_RECONSTRUCTED_PATH"
cat >"$TMP/condition-chains.lua" <<'LUA'
local function classify(value)
    if value == "alpha" or value == "beta" or value == "gamma" then
        return "matched"
    end
    return "other"
end
local function choose(first, second, third)
    if first or second or third then return "truthy" end
    return "falsy"
end
return classify, choose
LUA
"$BYTEVEIL_LUAC51" -o "$TMP/condition-chains.luac" "$TMP/condition-chains.lua"
"$BIN" --bytecode "$TMP/condition-chains.luac" --format lua > "$TMP/condition-chains.reconstructed.lua"
if grep -q 'PC dispatcher' "$TMP/condition-chains.reconstructed.lua"; then
    echo "a short-circuit condition chain was not reconstructed structurally" >&2
    exit 1
fi
"$BYTEVEIL_LUAC51" -p "$TMP/condition-chains.reconstructed.lua"
CONDITION_ORIGINAL_PATH="$TMP/condition-chains.lua"
CONDITION_RECONSTRUCTED_PATH="$TMP/condition-chains.reconstructed.lua"
if command -v cygpath >/dev/null 2>&1; then
    CONDITION_ORIGINAL_PATH="$(cygpath -m "$CONDITION_ORIGINAL_PATH")"
    CONDITION_RECONSTRUCTED_PATH="$(cygpath -m "$CONDITION_RECONSTRUCTED_PATH")"
fi
"$BYTEVEIL_LUA51" "$ROOT/tests/lua51_condition_chain_runtime.lua" "$CONDITION_ORIGINAL_PATH" "$CONDITION_RECONSTRUCTED_PATH"
cat >"$TMP/loop-breaks.lua" <<'LUA'
local function tally(n)
    local total = 0
    for i = 1, n do
        if i == 3 then break end
        total = total + i
    end
    local keys = 0
    for key, value in ipairs({"a", "b", "c"}) do
        if key == 2 then break end
        keys = keys + key
    end
    local countdown = 0
    while n > 0 do
        if n == 1 then break end
        countdown = countdown + 1
        n = n - 1
    end
    local repeated = 0
    repeat
        repeated = repeated + 1
        if repeated == 2 then break end
    until repeated > 10
    return total, keys, countdown, n, repeated
end
return tally
LUA
"$BYTEVEIL_LUAC51" -o "$TMP/loop-breaks.luac" "$TMP/loop-breaks.lua"
"$BIN" --bytecode "$TMP/loop-breaks.luac" --format lua > "$TMP/loop-breaks.reconstructed.lua"
if grep -q 'PC dispatcher' "$TMP/loop-breaks.reconstructed.lua"; then
    echo "structured loops containing break still use a PC dispatcher" >&2
    exit 1
fi
"$BYTEVEIL_LUAC51" -p "$TMP/loop-breaks.reconstructed.lua"
LOOP_BREAKS_ORIGINAL_PATH="$TMP/loop-breaks.lua"
LOOP_BREAKS_RECONSTRUCTED_PATH="$TMP/loop-breaks.reconstructed.lua"
if command -v cygpath >/dev/null 2>&1; then
    LOOP_BREAKS_ORIGINAL_PATH="$(cygpath -m "$LOOP_BREAKS_ORIGINAL_PATH")"
    LOOP_BREAKS_RECONSTRUCTED_PATH="$(cygpath -m "$LOOP_BREAKS_RECONSTRUCTED_PATH")"
fi
"$BYTEVEIL_LUA51" "$ROOT/tests/lua51_loop_break_runtime.lua" "$LOOP_BREAKS_ORIGINAL_PATH" "$LOOP_BREAKS_RECONSTRUCTED_PATH"
printf 'Lua 5.1 reconstruction tests: PASS\n'
