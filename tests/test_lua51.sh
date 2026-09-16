#!/usr/bin/env bash
set -u
BIN="${1:-./build/byteveil}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
"$BIN" --version | grep -q '^ByteVeil 0.4.7'
"$BIN" --bytecode "$ROOT/tests/fixtures/lua51-sample.luac" --format json >"$TMP/a.json"
python3 - "$TMP/a.json" <<'PY'
import json, sys
x=json.load(open(sys.argv[1]))
assert x['format'] == 'Lua 5.1 bytecode'
assert x['root_function']['instructions']
assert x['root_function']['children']
PY
"$BIN" --bytecode "$ROOT/tests/fixtures/lua51-sample.luac" --disassemble >"$TMP/dis"
grep -q '^function 0' "$TMP/dis"
grep -q 'CLOSURE' "$TMP/dis"
"$BIN" --bytecode "$ROOT/tests/fixtures/lua51-sample.luac" --cfg "$TMP/graph.dot" >/dev/null
grep -q '^digraph lua51_cfg' "$TMP/graph.dot"
"$BIN" --bytecode "$ROOT/tests/fixtures/lua51-sample.luac" --format lua >"$TMP/diag.lua"
grep -q '^-- ByteVeil Lua 5.1 lifted' "$TMP/diag.lua"
printf 'Lua 5.1 reader tests: PASS\n'
