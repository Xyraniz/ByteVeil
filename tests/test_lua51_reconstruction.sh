#!/usr/bin/env bash
set -euo pipefail
BIN="${1:-./build/byteveil}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if ! command -v luac5.1 >/dev/null 2>&1 || ! command -v lua5.1 >/dev/null 2>&1; then
    printf 'Lua 5.1 reconstruction tests: SKIP (lua5.1/luac5.1 not installed)\n'
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
luac5.1 -o "$TMP/control.luac" "$TMP/control.lua"
"$BIN" --bytecode "$TMP/control.luac" --format lua > "$TMP/reconstructed.lua"
lua5.1 -e "assert(loadfile('$TMP/reconstructed.lua'))"
grep -q '^for ' "$TMP/reconstructed.lua"
grep -q '^while ' "$TMP/reconstructed.lua"
grep -q '^repeat$' "$TMP/reconstructed.lua"
grep -q 'if ' "$TMP/reconstructed.lua"
grep -q 'elseif\|else' "$TMP/reconstructed.lua"
! grep -q 'unsupported opcode retained' "$TMP/reconstructed.lua"
printf 'Lua 5.1 reconstruction tests: PASS\n'
