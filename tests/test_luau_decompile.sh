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
if "$BIN" --format lua "$TMP/complex.luau" >"$TMP/out.lua" 2>"$TMP/err.txt"; then
    echo 'expected Luau decompiler integrity failure, got success' >&2
    cat "$TMP/out.lua" >&2
    exit 1
fi
grep -q 'structurally incomplete output' "$TMP/err.txt"
printf 'Luau integrity regression: PASS\n'
