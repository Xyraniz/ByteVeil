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
"$BYTEVEIL_PYTHON" - "$TMP/binary-strings.luac" "$TMP/setlist-extra.luac" "$TMP/bad-jump.luac" "$TMP/bad-constant.luac" "$TMP/bad-register.luac" "$TMP/missing-extra.luac" "$TMP/generic-for.luac" "$TMP/cyclic-jump.luac" "$TMP/infinite-loop.luac" "$TMP/test-repeat.luac" "$TMP/readable-coverage.luac" "$TMP/closure-local.luac" "$TMP/closure-nested.luac" "$TMP/closure-truncated.luac" "$TMP/closure-invalid-kind.luac" "$TMP/closure-invalid-source.luac" "$TMP/closure-jump-into-binding.luac" <<'PY'
import struct, sys

def u32(value):
    return struct.pack("<I", value)

def lua_string(value):
    return u32(len(value) + 1) + value + b"\0"

def proto(code, constants, *, source=b"@synthetic", maxstack=2, nups=0, params=0, vararg=2,
          locals=(), upvalues=(), children=()):
    chunk = bytearray()
    chunk += lua_string(source)
    chunk += struct.pack("<iiBBBB", 0, 0, nups, params, vararg, maxstack)
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
    chunk += u32(len(children))
    for child in children:
        chunk += proto(**child)
    chunk += u32(len(code)) + b"".join(u32(1) for _ in code)
    chunk += u32(len(locals))
    for name, start, end in locals:
        chunk += lua_string(name) + u32(start) + u32(end)
    chunk += u32(len(upvalues)) + b"".join(lua_string(name) for name in upvalues)
    return chunk

def build(code, constants, **kwargs):
    return bytearray(b"\x1bLua\x51\x00\x01\x04\x04\x04\x08\x00") + proto(code, constants, **kwargs)

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

# Core register/upvalue operations must survive the readable route.  The
# LOADBOOL C=1 skips the following LOADNIL instruction exactly as the VM does.
getupval = 4
setupval = 8
self_op = 11
vararg_one = 37 | (2 << 23)
loadnil_r1 = 3 | (1 << 6) | (1 << 23)
loadbool_skip = 2 | (1 << 23) | (1 << 14)
loadnil_skipped_r2 = 3 | (2 << 6) | (2 << 23)
open(sys.argv[11], "wb").write(build(
    [getupval, setupval, self_op, vararg_one, loadnil_r1, loadbool_skip,
     loadnil_skipped_r2, return_empty], [], maxstack=3, nups=1))

# Lua 5.1 CLOSURE consumes exactly child.nups pseudo-instructions.  The
# capture binding's A field is deliberately invalid here: the VM reads B only,
# so accepting this valid local capture proves ByteVeil does not mis-validate
# it as an independently executed MOVE A = B.
closure_r1_child0 = 36 | (1 << 6)
binding_move_r255_from_r0 = 0 | (255 << 6)
getupval_r0 = 4
open(sys.argv[12], "wb").write(build(
    [loadk, closure_r1_child0, binding_move_r255_from_r0, ret], [(4, b"outer")], maxstack=2,
    children=[dict(code=[getupval_r0, ret], constants=[], maxstack=1, nups=1)]))

# A nested closure inherits an upvalue through GETUPVAL.  The second binding
# is also pseudo-bytecode and must not appear as a standalone assignment.
closure_r0_child0 = 36
binding_getupval_r254_from_u0 = 4 | (254 << 6)
open(sys.argv[13], "wb").write(build(
    [loadk, closure_r1_child0, binding_move_r255_from_r0, ret], [(4, b"root")], maxstack=2,
    children=[dict(code=[closure_r0_child0, binding_getupval_r254_from_u0, ret], constants=[], maxstack=1, nups=1,
                   children=[dict(code=[getupval_r0, ret], constants=[], maxstack=1, nups=1)])]))

# Malformed closures fail visibly instead of treating arbitrary following
# bytecode as a capture operation.
open(sys.argv[14], "wb").write(build(
    [closure_r1_child0], [], maxstack=2,
    children=[dict(code=[getupval_r0, ret], constants=[], maxstack=1, nups=1)]))
open(sys.argv[15], "wb").write(build(
    [closure_r1_child0, loadk, ret], [(4, b"not a capture")], maxstack=2,
    children=[dict(code=[getupval_r0, ret], constants=[], maxstack=1, nups=1)]))
binding_move_from_bad_register = 0 | (7 << 23)
open(sys.argv[16], "wb").write(build(
    [closure_r1_child0, binding_move_from_bad_register, ret], [], maxstack=2,
    children=[dict(code=[getupval_r0, ret], constants=[], maxstack=1, nups=1)]))
jump_to_closure_binding = 22 | ((131071 + 1) << 14)  # pc 0 -> pc 2
open(sys.argv[17], "wb").write(build(
    [jump_to_closure_binding, closure_r1_child0, binding_move_r255_from_r0, ret], [], maxstack=2,
    children=[dict(code=[getupval_r0, ret], constants=[], maxstack=1, nups=1)]))
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
"$BIN" --bytecode "$TMP/readable-coverage.luac" --format lua >"$TMP/readable-coverage.lua"
grep -q '^r0 = __upvalue_0$' "$TMP/readable-coverage.lua"
grep -q '^__upvalue_0 = r0$' "$TMP/readable-coverage.lua"
grep -q '^r1 = r0$' "$TMP/readable-coverage.lua"
grep -q '^r0 = r0\[r0\]$' "$TMP/readable-coverage.lua"
grep -q '^r1 = nil$' "$TMP/readable-coverage.lua"
grep -q '^r0 = true$' "$TMP/readable-coverage.lua"
grep -q '^r0 = \.\.\.$' "$TMP/readable-coverage.lua"
if grep -q '^r2 = nil$' "$TMP/readable-coverage.lua"; then
    echo "LOADBOOL C=1 did not skip the following instruction" >&2
    exit 1
fi
"$BIN" --bytecode "$TMP/closure-local.luac" --format json >"$TMP/closure-local.json"
"$BIN" --bytecode "$TMP/closure-local.luac" --disassemble >"$TMP/closure-local.dis"
"$BIN" --bytecode "$TMP/closure-local.luac" --format lua >"$TMP/closure-local.lua"
"$BIN" --bytecode "$TMP/closure-nested.luac" --format lua >"$TMP/closure-nested.lua"
"$BYTEVEIL_PYTHON" - "$TMP/closure-local.json" <<'PY'
import json, sys
root = json.load(open(sys.argv[1]))["root_function"]
closure, binding = root["instructions"][1:3]
assert closure["opcode_name"] == "CLOSURE"
assert closure["captures"] == [{
    "slot": 0, "binding_pc": 2, "kind": "local", "source_index": 0
}]
assert binding["closure_binding_for_pc"] == 1
assert binding["capture_slot"] == 0
PY
grep -q 'CLOSURE.*CAPTURES=1.*\[0:local 0 at 2\]' "$TMP/closure-local.dis"
grep -q 'CLOSURE_BINDING owner=1 slot=0' "$TMP/closure-local.dis"
grep -q '^-- ByteVeil: CLOSURE pc 1 captures upvalue 0 from local register r0 (binding pc 2)$' "$TMP/closure-local.lua"
grep -q '^    __byteveil_f1_r0 = r0$' "$TMP/closure-local.lua"
if grep -q '^r255 = r0$' "$TMP/closure-local.lua"; then
    echo "CLOSURE local capture was emitted as a standalone MOVE" >&2
    exit 1
fi
grep -q '^    -- ByteVeil: CLOSURE pc 0 captures upvalue 0 from parent upvalue r0 (binding pc 1)$' "$TMP/closure-nested.lua"
grep -q '^        __byteveil_f2_r0 = r0$' "$TMP/closure-nested.lua"
if grep -q '__byteveil_f1_r254 = r0' "$TMP/closure-nested.lua"; then
    echo "CLOSURE upvalue capture was emitted as a standalone GETUPVAL" >&2
    exit 1
fi
"$BYTEVEIL_PYTHON" - "$TMP/setlist-extra.json" <<'PY'
import json, sys
instructions = json.load(open(sys.argv[1]))["root_function"]["instructions"]
assert instructions[2]["setlist_block"] == 2
assert instructions[3]["opcode_name"] == "EXTRAARG"
assert instructions[3]["extra_word"] is True
PY
for case in bad-jump bad-constant bad-register missing-extra closure-truncated closure-invalid-kind closure-invalid-source closure-jump-into-binding; do
    if "$BIN" --bytecode "$TMP/$case.luac" --format json >"$TMP/$case.out" 2>"$TMP/$case.err"; then
        echo "malformed Lua 5.1 case $case was accepted" >&2
        exit 1
    fi
done
grep -q 'jump target is not an instruction boundary' "$TMP/bad-jump.err"
grep -q 'constant index out of range' "$TMP/bad-constant.err"
grep -q 'register B out of range' "$TMP/bad-register.err"
grep -q 'SETLIST is missing its extra block word' "$TMP/missing-extra.err"
grep -q 'CLOSURE capture bindings truncated' "$TMP/closure-truncated.err"
grep -q 'CLOSURE capture binding must be MOVE or GETUPVAL' "$TMP/closure-invalid-kind.err"
grep -q 'CLOSURE local capture B out of range' "$TMP/closure-invalid-source.err"
grep -q 'jump target is not an instruction boundary' "$TMP/closure-jump-into-binding.err"
printf 'Lua 5.1 reader tests: PASS\n'
