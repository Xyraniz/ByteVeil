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
    functions[index] = function() return index end
end
return functions
LUA
cat >"$TMP/generic-closures.lua" <<'LUA'
local functions = {}
for key, value in ipairs({ "alpha", "beta", "gamma" }) do
    functions[key] = function() return key, value end
end
return functions
LUA
"$BYTEVEIL_LUAC51" -o "$TMP/numeric-closures.luac" "$TMP/numeric-closures.lua"
"$BYTEVEIL_LUAC51" -o "$TMP/generic-closures.luac" "$TMP/generic-closures.lua"
"$BIN" --bytecode "$TMP/numeric-closures.luac" --format lua > "$TMP/numeric-closures.reconstructed.lua"
"$BIN" --bytecode "$TMP/generic-closures.luac" --format lua > "$TMP/generic-closures.reconstructed.lua"
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
"$BYTEVEIL_LUA51" -e "local original=assert(loadfile('$NUMERIC_ORIGINAL_PATH'))(); local reconstructed=assert(loadfile('$NUMERIC_RECONSTRUCTED_PATH'))(); for i=1,3 do assert(original[i]()==i); assert(reconstructed[i]()==original[i]()) end; local original_generic=assert(loadfile('$GENERIC_ORIGINAL_PATH'))(); local reconstructed_generic=assert(loadfile('$GENERIC_RECONSTRUCTED_PATH'))(); local expected={'alpha','beta','gamma'}; for i=1,3 do local key,value=original_generic[i](); local got_key,got_value=reconstructed_generic[i](); assert(key==i and value==expected[i]); assert(got_key==key and got_value==value) end"
printf 'Lua 5.1 reconstruction tests: PASS\n'
