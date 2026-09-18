#!/usr/bin/env bash
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"
byteveil_find_python
BIN="${1:-./build/byteveil}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cat >"$TMP/expressions.luau" <<'LUA'
local function add(a, b)
    local value = a + b
    return value
end
local total = add(1, 2)
return total
LUA

# The integrity guard must reject an unsafe reconstruction rather than report
# success with a syntactically plausible but semantically incomplete program.
if "$BIN" --format lua "$TMP/expressions.luau" >"$TMP/out.lua" 2>"$TMP/err.txt"; then
    test ! -s "$TMP/err.txt"
else
    grep -Eq 'structurally incomplete|reconstruction incomplete|decompiler exception' "$TMP/err.txt"
fi

cat >"$TMP/loops.luau" <<'LUA'
local total = 0
for i = 1, 3 do
    total += i
end
return total
LUA
"$BIN" --format json "$TMP/loops.luau" >"$TMP/loops.json"
"$BYTEVEIL_PYTHON" - "$TMP/loops.json" <<'PY'
import json, sys
x = json.load(open(sys.argv[1]))
f = x['root_function']
ops = [i['opcode_name'] for i in f['instructions']]
assert 'FORNPREP' in ops and 'FORNLOOP' in ops
assert f['basic_blocks']
assert f['cfg_analysis']['immediate_dominators']
assert f['cfg_analysis']['sccs']
assert f['scopes']
assert all('uses' in i and 'source_block' in i and 'destination_register' in i for i in f['instructions'])
assert any(i['has_side_effects'] for i in f['instructions'])
assert any(i['is_pure'] for i in f['instructions'])
assert all('predecessors' in b for b in f['basic_blocks'])
assert 'ssa' in f and 'phi_nodes' in f['ssa']
PY

printf '\x1bLuau' >"$TMP/truncated.luau"
if "$BIN" --bytecode "$TMP/truncated.luau" >"$TMP/bad.out" 2>"$TMP/bad.err"; then
    echo 'truncated Luau input unexpectedly succeeded' >&2
    exit 1
fi
grep -Eq 'error:|failed|truncated|bytecode' "$TMP/bad.err"
printf 'Luau regression tests: PASS\n'
