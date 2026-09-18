#!/usr/bin/env bash
set -euo pipefail
BIN="${1:-./build/byteveil}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cat >"$TMP/complex.luau" <<'LUA'
local function classify(value)
    if value < 0 then return "negative" else return "positive" end
end
local total = 0
for i = 1, 3 do
    total += i
end
return classify(total)
LUA
"$BIN" --format lua "$TMP/complex.luau" >"$TMP/out.lua" 2>"$TMP/err.txt"
test ! -s "$TMP/err.txt"
grep -q '^-- ByteVeil Luau register-state reconstruction' "$TMP/out.lua"
grep -q 'while true do' "$TMP/out.lua"
grep -q 'byteveil_functions\[1\]' "$TMP/out.lua"
grep -q 'FORN\|get(4) + get(3)\|pc = 7' "$TMP/out.lua"
! grep -q 'unsupported opcode' "$TMP/out.lua"
printf 'Luau register-state reconstruction: PASS\n'
