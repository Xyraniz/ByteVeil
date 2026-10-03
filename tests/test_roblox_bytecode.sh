#!/usr/bin/env bash
set -euo pipefail
BIN="${1:?ByteVeil executable path required}"
RUNNER="${2:?Luau compiler helper path required}"
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"
byteveil_find_python
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cat >"$TMP/roblox_sample.luau" <<'LUA'
local function countValues(values)
    local total = 0
    for _, value in pairs(values) do
        if value ~= nil then
            total += value
        end
    end
    return total
end
return countValues({ 1, 2, 3 })
LUA

# Produce normal Luau bytecode and encode only instruction opcodes using the
# corpus-observed Roblox encoding. No fixture bytecode is copied from a game.
"$RUNNER" --compile-roblox 2 "$TMP/roblox_sample.luau" "$TMP/encoded.luac"
"$BIN" --format json "$TMP/encoded.luac" >"$TMP/ir.json"
grep -q '"opcode_name":"FORGLOOP"' "$TMP/ir.json"
grep -q '"opcode_name":"JUMPXEQKNIL"' "$TMP/ir.json"

# Confirm the reconstructed source is accepted by the Luau compiler without
# executing it, and that the optional 24-byte Roblox footer remains readable.
"$BIN" --format lua "$TMP/encoded.luac" -o "$TMP/reconstructed.luau"
"$RUNNER" --compile 0 "$TMP/reconstructed.luau" "$TMP/reconstructed.luac"
cp "$TMP/encoded.luac" "$TMP/encoded-with-footer.luac"
"$BYTEVEIL_PYTHON" - "$TMP/encoded-with-footer.luac" <<'PY'
import sys
with open(sys.argv[1], "ab") as output:
    output.write(bytes(24))
PY
"$BIN" --format prototypes "$TMP/encoded-with-footer.luac" >"$TMP/prototypes.txt"
grep -q '^function 0 ' "$TMP/prototypes.txt"

head -c -1 "$TMP/encoded.luac" >"$TMP/truncated.luac"
if "$BIN" --format json "$TMP/truncated.luac" >"$TMP/bad.json" 2>"$TMP/truncated.err"; then
    echo "truncated Roblox bytecode was accepted" >&2
    exit 1
fi
grep -q '^error:' "$TMP/truncated.err"

# Repeated calls used to emit two uniquely named locals per instruction and
# exceed Luau's 200-local function limit on otherwise ordinary bytecode.
{
    cat <<'LUA'
local function identity(value)
    return value
end
local total = 0
LUA
    for value in $(seq 1 110); do
        printf 'total += identity(%s)\n' "$value"
    done
    cat <<'LUA'
return total
LUA
} >"$TMP/many_calls.luau"
"$RUNNER" --compile 0 "$TMP/many_calls.luau" "$TMP/many_calls.luac"
"$BIN" --format lua "$TMP/many_calls.luac" -o "$TMP/many_calls.reconstructed.luau"
"$RUNNER" --compile 0 "$TMP/many_calls.reconstructed.luau" "$TMP/many_calls.roundtrip.luac"

printf 'Roblox bytecode decoding and source reconstruction: PASS\n'
