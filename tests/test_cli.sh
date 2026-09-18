#!/usr/bin/env bash
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"
byteveil_find_python
BIN="${1:-./build/byteveil}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cat >"$TMP/sample.luau" <<'LUA'
local x = 1 + 2
for i = 1, 3 do
    x += i
end
return x
LUA
"$BIN" --version | grep -q '^ByteVeil '
"$BIN" --format json "$TMP/sample.luau" >"$TMP/a.json"
"$BIN" --format json "$TMP/sample.luau" >"$TMP/b.json"
grep -q '"root_function"' "$TMP/a.json"
grep -q '"opcode_name"' "$TMP/a.json"
grep -q '"cfg_analysis"' "$TMP/a.json"
grep -q '"immediate_dominators"' "$TMP/a.json"
grep -q '"ssa"' "$TMP/a.json"
grep -q '"phi_nodes"' "$TMP/a.json"
grep -q '"dataflow"' "$TMP/a.json"
grep -q '"liveness"' "$TMP/a.json"
grep -q '"immediate_post_dominators"' "$TMP/a.json"
grep -q '"loop_regions"' "$TMP/a.json"
grep -q '"register_first_use"' "$TMP/a.json"
grep -q '"register_last_use"' "$TMP/a.json"
cmp "$TMP/a.json" "$TMP/b.json"
"$BIN" --format structured "$TMP/sample.luau" >"$TMP/structured"
grep -q '^-- ByteVeil structured control-flow reconstruction' "$TMP/structured"
grep -q 'loop-header' "$TMP/structured"
grep -q 'join=block_' "$TMP/structured"
cat >"$TMP/arithmetic.luau" <<'LUA'
local function f(value)
    if value % 2 == 0 then return value + 7 end
    return value * 3 - 1
end
return f(5)
LUA
"$BIN" --format json "$TMP/arithmetic.luau" >"$TMP/arithmetic.json"
"$BYTEVEIL_PYTHON" - "$TMP/arithmetic.json" <<'PY'
import json, sys
x = json.load(open(sys.argv[1]))
assert x["root_function"]["dataflow"]["unknown_instructions"] >= 0
assert x["root_function"]["instructions"]
def check_phis(fn):
    for phi in fn["ssa"]["phi_nodes"]:
        assert len(phi["incoming_blocks"]) == len(phi["incoming"])
    for child in fn["children"]:
        check_phis(child)
check_phis(x["root_function"])
PY
"$BIN" --disassemble "$TMP/sample.luau" >"$TMP/disassembly"
grep -q '^function 0' "$TMP/disassembly"
grep -q 'LOAD' "$TMP/disassembly"
grep -q 'idom=block_' "$TMP/disassembly"
grep -q 'phi=r' "$TMP/disassembly"
"$BIN" --cfg "$TMP/graph.dot" "$TMP/sample.luau" >/dev/null
grep -q '^digraph byteveil_cfg' "$TMP/graph.dot"
grep -q 'idom=' "$TMP/graph.dot"
printf 'MoonSec V3\n' >"$TMP/protected.lua"
"$BIN" --analyze "$TMP/protected.lua" | grep -q 'moonsec_marker: yes'
printf 'error("MUST_NOT_EXECUTE")\n' >"$TMP/noexec.luau"
"$BIN" --analyze "$TMP/noexec.luau" >"$TMP/static-report" 2>"$TMP/static-error"
grep -q '^format:' "$TMP/static-report"
! grep -q 'MUST_NOT_EXECUTE' "$TMP/static-error"
printf '\033Luau' >"$TMP/truncated.luau"
if "$BIN" --bytecode "$TMP/truncated.luau" >/dev/null 2>&1; then exit 1; fi
echo "synthetic CLI tests: PASS"

if "$BIN" --format definitely-not-a-format "$TMP/sample.luau" >/dev/null 2>&1; then
    echo "unknown format was accepted" >&2
    exit 1
fi
if "$BIN" --timeout nope "$TMP/sample.luau" >/dev/null 2>&1; then
    echo "invalid timeout was accepted" >&2
    exit 1
fi
