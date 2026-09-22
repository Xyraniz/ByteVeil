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
"$BYTEVEIL_PYTHON" - "$TMP/binary-strings.luac" "$TMP/setlist-extra.luac" "$TMP/bad-jump.luac" "$TMP/bad-constant.luac" "$TMP/bad-register.luac" "$TMP/missing-extra.luac" "$TMP/generic-for.luac" "$TMP/cyclic-jump.luac" "$TMP/infinite-loop.luac" "$TMP/test-repeat.luac" <<'PY'
import struct, sys

def u32(value):
    return struct.pack("<I", value)

def lua_string(value):
    return u32(len(value) + 1) + value + b"\0"

def build(code, constants, *, source=b"@synthetic", maxstack=2, nups=0, locals=(), upvalues=()):
    chunk = bytearray(b"\x1bLua\x51\x00\x01\x04\x04\x04\x08\x00")
    chunk += lua_string(source)
    chunk += struct.pack("<iiBBBB", 0, 0, nups, 0, 2, maxstack)
    chunk += u32(len(code)) + b"".join(u32(word) for word in code)
    chunk += u32(len(constants))
    for tag, value in constants:
        chunk += bytes([tag])
        if tag == 1:
            chunk += bytes([bool(value)])
        elif tag == 3:
            chunk += struct.pack("<d", value)
        elif tag == 4:
            chunk += lua_string(value)
    chunk += u32(0)  # child prototypes
    chunk += u32(len(code)) + b"".join(u32(1) for _ in code)
    chunk += u32(len(locals))
    for name, start, end in locals:
        chunk += lua_string(name) + u32(start) + u32(end)
    chunk += u32(len(upvalues)) + b"".join(lua_string(name) for name in upvalues)
    return chunk

source = b"@synthetic\x1f\xff"
literal = b'quote:" newline:\n nul:\0 ctrl:\x1f utf8:\xc3\xa9 raw:\xff'
local_name = b"local\x1f\xff"
upvalue_name = b"upvalue\0\xff"
loadk = 1  # LOADK A=0 Bx=0
close = 35 | (1 << 6)  # CLOSE A=1; keeps LOADK separate from RETURN folding
ret = 30 | (2 << 23)  # RETURN A=0 B=2 C=0
open(sys.argv[1], "wb").write(build([loadk, close, ret], [(4, literal)], source=source, nups=1,
    locals=[(local_name, 0, 3)], upvalues=[upvalue_name]))

newtable = 10
loadk_r1 = 1 | (1 << 6)
setlist_extended = 34 | (1 << 23)  # A=0 B=1 C=0; next word is block number
open(sys.argv[2], "wb").write(build([newtable, loadk_r1, setlist_extended, 2, ret], [(4, b"value")]))

jump_far = 22 | ((131071 + 100) << 14)
open(sys.argv[3], "wb").write(build([jump_far, ret], []))
load_missing_constant = 1 | (3 << 14)
open(sys.argv[4], "wb").write(build([load_missing_constant, ret], [(0, None)]))
move_bad_register = 0 | (7 << 23)
open(sys.argv[5], "wb").write(build([move_bad_register, ret], []))
open(sys.argv[6], "wb").write(build([setlist_extended], []))

# JMP -> TFORLOOP -> backward JMP is Lua 5.1's generic-for layout.  The
# source lifter must consume the whole CFG shape, rather than following the
# back-edge indefinitely.
jmp_to_tfor = 22 | ((131071 + 1) << 14)       # pc 0 -> pc 2
move_body = 0 | (3 << 23)                     # MOVE A=0 B=3
tforloop = 33 | (1 << 14)                     # TFORLOOP A=0 C=1
jmp_to_body = 22 | ((131071 - 3) << 14)       # pc 3 -> pc 1
return_empty = 30 | (1 << 23)                 # RETURN A=0 B=1
open(sys.argv[7], "wb").write(build(
    [jmp_to_tfor, move_body, tforloop, jmp_to_body, return_empty], [], maxstack=4))

# A valid but irreducible self-jump must terminate with an explicit marker;
# it is not safe for a readable renderer to execute its control flow forever.
jump_to_self = 22 | ((131071 - 1) << 14)      # pc 0 -> pc 0
open(sys.argv[8], "wb").write(build([jump_to_self], [], maxstack=1))

# A closed loop with one entry-anchored latch is safe to render as `while
# true`; the trailing return is unreachable but retained as a diagnostic line.
move_self = 0                                  # MOVE A=0 B=0
jump_to_entry = 22 | ((131071 - 2) << 14)     # pc 1 -> pc 0
open(sys.argv[9], "wb").write(build(
    [move_self, jump_to_entry, return_empty], [], maxstack=1))

# TEST followed by a backward JMP is the compiler's repeat/until terminator.
# C=0 means the loop completes when register A is truthy.
test_truthy = 26                            # TEST A=0 C=0
jump_back_to_repeat = 22 | ((131071 - 3) << 14)  # pc 2 -> pc 0
open(sys.argv[10], "wb").write(build(
    [move_self, test_truthy, jump_back_to_repeat, return_empty], [], maxstack=1))
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
"$BIN" --bytecode "$TMP/setlist-extra.luac" --disassemble >"$TMP/setlist-extra.dis"
"$BIN" --bytecode "$TMP/setlist-extra.luac" --format json >"$TMP/setlist-extra.json"
"$BIN" --bytecode "$TMP/setlist-extra.luac" --format lua >"$TMP/setlist-extra.lua"
grep -q 'SETLIST .* block=2' "$TMP/setlist-extra.dis"
grep -q 'EXTRAARG raw=2' "$TMP/setlist-extra.dis"
grep -q 'r0\[51\] = r1' "$TMP/setlist-extra.lua"
"$BIN" --bytecode "$TMP/generic-for.luac" --format lua >"$TMP/generic-for.lua"
grep -q '^for r3 in r0, r1, r2 do$' "$TMP/generic-for.lua"
if grep -q 'stopped at repeated control-flow' "$TMP/generic-for.lua"; then
    echo "generic for was not structurally reconstructed" >&2
    exit 1
fi
"$BIN" --bytecode "$TMP/cyclic-jump.luac" --format lua >"$TMP/cyclic-jump.lua"
grep -q 'function 0 stopped at repeated control-flow pc 0 (unstructured cycle)' "$TMP/cyclic-jump.lua"
"$BIN" --bytecode "$TMP/infinite-loop.luac" --format lua >"$TMP/infinite-loop.lua"
grep -q '^while true do$' "$TMP/infinite-loop.lua"
if grep -q 'stopped at repeated control-flow' "$TMP/infinite-loop.lua"; then
    echo "closed unconditional loop was not structurally reconstructed" >&2
    exit 1
fi
"$BIN" --bytecode "$TMP/test-repeat.luac" --format lua >"$TMP/test-repeat.lua"
grep -q '^repeat$' "$TMP/test-repeat.lua"
grep -q '^until r0$' "$TMP/test-repeat.lua"
if grep -q 'stopped at repeated control-flow' "$TMP/test-repeat.lua"; then
    echo "TEST-based repeat loop was not structurally reconstructed" >&2
    exit 1
fi
"$BYTEVEIL_PYTHON" - "$TMP/setlist-extra.json" <<'PY'
import json, sys
instructions = json.load(open(sys.argv[1]))["root_function"]["instructions"]
assert instructions[2]["setlist_block"] == 2
assert instructions[3]["opcode_name"] == "EXTRAARG"
assert instructions[3]["extra_word"] is True
PY
for case in bad-jump bad-constant bad-register missing-extra; do
    if "$BIN" --bytecode "$TMP/$case.luac" --format json >"$TMP/$case.out" 2>"$TMP/$case.err"; then
        echo "malformed Lua 5.1 case $case was accepted" >&2
        exit 1
    fi
done
grep -q 'jump target is not an instruction boundary' "$TMP/bad-jump.err"
grep -q 'constant index out of range' "$TMP/bad-constant.err"
grep -q 'register B out of range' "$TMP/bad-register.err"
grep -q 'SETLIST is missing its extra block word' "$TMP/missing-extra.err"
printf 'Lua 5.1 reader tests: PASS\n'
