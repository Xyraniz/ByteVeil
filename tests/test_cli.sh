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
cat >"$TMP/constants.luau" <<'LUA'
local payload = "quote:\34 newline:\10 nul:\0 ctrl:\31 utf8:é raw:\255"
local function child(named)
    local debugLocal = payload .. named
    return debugLocal
end
return child("ok")
LUA
"$BIN" --format json "$TMP/constants.luau" >"$TMP/constants.json"
"$BIN" --dump-constants "$TMP/constants.luau" >"$TMP/constants.txt"
"$BIN" --dump-prototypes "$TMP/constants.luau" >"$TMP/prototypes.txt"
"$BYTEVEIL_PYTHON" - "$TMP/constants.json" <<'PY'
import json, sys
module = json.load(open(sys.argv[1], encoding="utf-8"))
root = module["root_function"]
assert len(root["constant_table"]) == root["constants"]
assert len(root["upvalue_names"]) == root["upvalues"]
assert root["children"] and root["children"][0]["parent_id"] == root["id"]

functions = []
def collect(fn):
    functions.append(fn)
    for child in fn["children"]:
        collect(child)
collect(root)
strings = [constant for fn in functions for constant in fn["constant_table"] if constant["type"] == "string"]
payload = next(constant for constant in strings if constant["value"].startswith('quote:"'))
expected = b'quote:" newline:\n nul:\0 ctrl:\x1f utf8:\xc3\xa9 raw:\xff'
assert payload["string_bytes_hex"] == expected.hex()
for fn in functions:
    assert len(fn["constant_table"]) == fn["constants"]
    for local in fn["debug_locals"]:
        assert 0 <= local["register"] < fn["registers"]
        assert 0 <= local["start"] <= local["end"]
PY
grep -q '^function 0 .* constants:' "$TMP/constants.txt"
grep -q 'string value=.*quote:' "$TMP/constants.txt"
grep -q 'bytes=' "$TMP/constants.txt"
grep -q '^  function 1 ' "$TMP/prototypes.txt"
grep -q 'parent=0 prototype_index=0' "$TMP/prototypes.txt"
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
