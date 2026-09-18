#!/usr/bin/env bash
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"
byteveil_find_python
BIN="${1:-./build/byteveil}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
"$BIN" --version | grep -q '^ByteVeil 0.5.0'
"$BIN" --bytecode "$ROOT/tests/fixtures/lua51-sample.luac" --format json >"$TMP/a.json"
"$BYTEVEIL_PYTHON" - "$TMP/a.json" <<'PY'
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
"$BYTEVEIL_PYTHON" - "$TMP/binary-strings.luac" <<'PY'
import struct, sys

def u32(value):
    return struct.pack("<I", value)

def lua_string(value):
    return u32(len(value) + 1) + value + b"\0"

source = b"@synthetic\x1f\xff"
literal = b'quote:" newline:\n nul:\0 ctrl:\x1f utf8:\xc3\xa9 raw:\xff'
local_name = b"local\x1f\xff"
upvalue_name = b"upvalue\0\xff"
loadk = 1  # LOADK A=0 Bx=0
close = 35 | (1 << 6)  # CLOSE A=1; keeps LOADK separate from RETURN folding
ret = 30 | (2 << 23)  # RETURN A=0 B=2 C=0

chunk = bytearray(b"\x1bLua\x51\x00\x01\x04\x04\x04\x08\x00")
chunk += lua_string(source)
chunk += struct.pack("<iiBBBB", 0, 0, 1, 0, 2, 2)
chunk += u32(3) + u32(loadk) + u32(close) + u32(ret)
chunk += u32(1) + b"\x04" + lua_string(literal)
chunk += u32(0)  # child prototypes
chunk += u32(3) + u32(1) + u32(1) + u32(1)
chunk += u32(1) + lua_string(local_name) + u32(0) + u32(3)
chunk += u32(1) + lua_string(upvalue_name)
open(sys.argv[1], "wb").write(chunk)
PY
"$BIN" --bytecode "$TMP/binary-strings.luac" --format json >"$TMP/binary-strings.json"
"$BIN" --bytecode "$TMP/binary-strings.luac" --dump-constants >"$TMP/binary-strings.txt"
"$BIN" --bytecode "$TMP/binary-strings.luac" --format lua >"$TMP/binary-strings.lua"
"$BYTEVEIL_PYTHON" - "$TMP/binary-strings.json" <<'PY'
import json, sys
root = json.load(open(sys.argv[1], encoding="utf-8"))["root_function"]
assert root["source_bytes_hex"] == b"@synthetic\x1f\xff".hex()
assert root["locals"][0]["name_bytes_hex"] == b"local\x1f\xff".hex()
assert root["upvalue_bytes_hex"][0] == b"upvalue\0\xff".hex()
constant = root["constant_table"][0]
expected = b'quote:" newline:\n nul:\0 ctrl:\x1f utf8:\xc3\xa9 raw:\xff'
assert constant["type"] == "string"
assert constant["string_bytes_hex"] == expected.hex()
assert "\\000" in constant["lua"] and "\\255" in constant["lua"]
PY
grep -q 'bytes=.*ff$' "$TMP/binary-strings.txt"
grep -q '\\000' "$TMP/binary-strings.lua"
grep -q '^r0 = ' "$TMP/binary-strings.lua"
printf 'Lua 5.1 reader tests: PASS\n'
