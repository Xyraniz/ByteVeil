#!/usr/bin/env bash
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"
byteveil_find_python
byteveil_find_lua51 || true
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
"$BYTEVEIL_PYTHON" - "$TMP/binary-strings.luac" "$TMP/setlist-extra.luac" "$TMP/bad-jump.luac" "$TMP/bad-constant.luac" "$TMP/bad-register.luac" "$TMP/missing-extra.luac" "$TMP/generic-for.luac" "$TMP/cyclic-jump.luac" "$TMP/infinite-loop.luac" "$TMP/test-repeat.luac" "$TMP/readable-coverage.luac" "$TMP/closure-local.luac" "$TMP/closure-nested.luac" "$TMP/closure-truncated.luac" "$TMP/closure-invalid-kind.luac" "$TMP/closure-invalid-source.luac" "$TMP/closure-jump-into-binding.luac" "$TMP/testset-and.luac" "$TMP/testset-or.luac" "$TMP/move-overwritten-source.luac" "$TMP/eq-a1.luac" "$TMP/eq-a0.luac" "$TMP/branch-range-escape.luac" "$TMP/open-call-chain.luac" "$TMP/open-vararg-call.luac" "$TMP/open-return-call.luac" "$TMP/open-tailcall.luac" "$TMP/colon-self-call.luac" "$TMP/colon-open-call.luac" "$TMP/nested-branch-exit.luac" "$TMP/colon-flow-entry.luac" "$TMP/open-setlist.luac" "$TMP/open-setlist-vararg.luac" "$TMP/open-branch-entry.luac" "$TMP/close-captured-register.luac" "$TMP/jump-a-ignored-captured-register.luac" "$TMP/conditional-jump-a-ignored-captured-register.luac" "$TMP/bad-jump-a-register.luac" "$TMP/multi-latch-loop.luac" "$TMP/generic-for-continue.luac" "$TMP/numeric-for-continue.luac" "$TMP/generic-for-nested-if.luac" "$TMP/single-latch-loop.luac" "$TMP/guarded-short-circuit.luac" "$TMP/guarded-single-short-circuit.luac" "$TMP/nested-shared-else.luac" "$TMP/nested-loops.luac" "$TMP/shared-return-guards.luac" "$TMP/shared-else-join.luac" "$TMP/shared-body-guards.luac" <<'PY'
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

# An unconditional self-jump is an empty infinite loop. The readable output
# preserves its non-terminating behavior without needing a PC dispatcher.
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

# TESTSET assigns A = B only on the branch that consumes the following JMP.
# C=0 is the compiler's short-circuit `and` shape; C=1 is its `or` shape.
jump_over_fallback = 22 | ((131071 + 1) << 14)  # pc 2 -> pc 4
loadk_fallback_r0 = 1 | (1 << 14)
testset_and = 27 | (1 << 23)                 # A=0 B=1 C=0
testset_or = 27 | (1 << 23) | (1 << 14)      # A=0 B=1 C=1
open(sys.argv[18], "wb").write(build(
    [loadk_r1, testset_and, jump_over_fallback, loadk_fallback_r0, ret],
    [(1, False), (4, b"fallback")], maxstack=2))
open(sys.argv[19], "wb").write(build(
    [loadk_r1, testset_or, jump_over_fallback, loadk_fallback_r0, ret],
    [(1, True), (4, b"fallback")], maxstack=2))

# A MOVE snapshots the source register.  Replacing that source later must not
# rewrite the copied value when a later CALL argument is rendered.
getglobal_object = 5
move_r1_from_r0 = 0 | (1 << 6)
getglobal_callback = 5 | (2 << 14)
call_r0_with_one_arg = 28 | (2 << 23) | (2 << 14)  # CALL A=0 B=2 C=2
open(sys.argv[20], "wb").write(build(
    [getglobal_object, move_r1_from_r0, getglobal_callback, call_r0_with_one_arg, ret],
    [(4, b"object"), (4, b"unused"), (4, b"callback")], maxstack=2))

# EQ A=1 jumps when the operands match; EQ A=0 jumps when they differ.  The
# structured body after the comparison therefore uses the opposite polarity.
def jmp(pc, target):
    return 22 | ((131071 + target - pc - 1) << 14)

def comparison_with_polarity(polarity):
    return 23 | (polarity << 6) | (1 << 14)  # EQ A=polarity B=R0 C=R1

loadk_then_r2 = 1 | (2 << 6)
loadk_else_r2 = 1 | (2 << 6) | (1 << 14)
return_r2 = 30 | (2 << 6) | (2 << 23)
for path_index, polarity in ((21, 1), (22, 0)):
    open(sys.argv[path_index], "wb").write(build(
        [comparison_with_polarity(polarity), jmp(1, 4), loadk_then_r2,
         jmp(3, 5), loadk_else_r2, return_r2],
        [(4, b"then"), (4, b"else")], maxstack=3))

# A nested conditional jump leaves the current then-range and reaches the
# shared tail.  The renderer must leave that edge visible instead of expanding
# the shared tail inside the nested branch and then printing it a second time.
open(sys.argv[23], "wb").write(build(
    [comparison_with_polarity(0), jmp(1, 5), comparison_with_polarity(0),
     jmp(3, 7), loadk_then_r2, loadk_else_r2, return_r2, return_r2],
    [(4, b"inside-range"), (4, b"range-tail")], maxstack=3))

# Open CALL results become the final argument expression of the next CALL.
# This is equivalent to consume("fixed", produce("payload")), including all
# values returned by produce.
def getglobal(a, bx):
    return 5 | (a << 6) | (bx << 14)

def loadk(a, bx):
    return 1 | (a << 6) | (bx << 14)

def call(a, b, c):
    return 28 | (a << 6) | (c << 14) | (b << 23)

def ret(a, b):
    return 30 | (a << 6) | (b << 23)

open(sys.argv[24], "wb").write(build(
    [getglobal(0, 0), loadk(1, 1), getglobal(2, 2), loadk(3, 3),
     call(2, 2, 0), call(0, 0, 1), ret(0, 1)],
    [(4, b"consume"), (4, b"fixed"), (4, b"produce"), (4, b"payload")], maxstack=4))

# An open VARARG followed by CALL B=0 must retain the full argument tail.
vararg_open = 37 | (1 << 6)
open(sys.argv[25], "wb").write(build(
    [getglobal(0, 0), vararg_open, call(0, 0, 1), ret(0, 1)],
    [(4, b"consume")], maxstack=2))

# CALL C=0 followed by RETURN B=0 is the source-level `return produce(...)`;
# a fixed result slot must not truncate the function's open result list.
open(sys.argv[26], "wb").write(build(
    [getglobal(0, 0), loadk(1, 1), call(0, 2, 0), ret(0, 0)],
    [(4, b"produce"), (4, b"payload")], maxstack=2))

# TAILCALL B=0 forwards all varargs and returns the callee's full result list.
tailcall_open = 29 | (0 << 6)
open(sys.argv[27], "wb").write(build(
    [getglobal(0, 0), vararg_open, tailcall_open],
    [(4, b"consume")], maxstack=2))

# SELF followed by a no-argument CALL uses a stable string method name and can
# be rendered as a colon call without exposing its temporary receiver slot.
self_ping = 11 | (1 << 23) | (257 << 14)  # SELF A=0 B=1 C=K1
open(sys.argv[28], "wb").write(build(
    [getglobal(1, 0), self_ping, call(0, 2, 1), ret(0, 1)],
    [(4, b"object"), (4, b"ping")], maxstack=2))

# Fused SELF + open-result CALL remains a method-call expression when its
# results flow directly into a variable-arity consumer.
self_ping_r1 = 11 | (1 << 6) | (2 << 23) | (258 << 14)  # SELF A=1 B=2 C=K2
open(sys.argv[29], "wb").write(build(
    [getglobal(0, 0), getglobal(2, 1), self_ping_r1, call(1, 2, 0), call(0, 0, 1), ret(0, 1)],
    [(4, b"consume"), (4, b"object"), (4, b"ping")], maxstack=3))

# The inner TEST branch exits its enclosing then-range to the shared return.
# ByteVeil can preserve this shared join with nested structured conditionals.
test = lambda a, c: 26 | (a << 6) | (c << 14)
open(sys.argv[30], "wb").write(build(
    [loadk(1, 0), getglobal(0, 1), test(0, 0), jmp(3, 9), getglobal(2, 2),
     test(2, 0), jmp(6, 10), loadk(1, 3), jmp(8, 10), loadk(1, 4), ret(1, 2),
     getglobal(0, 5), 29 | (1 << 23), ret(0, 0)],
    [(4, b"default"), (4, b"flag"), (4, b"inner"), (4, b"left"), (4, b"right"), (4, b"exit")], maxstack=3))

# One branch enters a CALL directly while the other first executes its
# adjacent SELF. The CALL must not be rendered with colon syntax on both paths.
open(sys.argv[31], "wb").write(build(
    [getglobal(0, 0), getglobal(1, 1), getglobal(2, 2), test(2, 0), jmp(4, 6),
     11 | (1 << 23) | (259 << 14), call(0, 2, 2), ret(0, 2)],
    [(4, b"plain"), (4, b"object"), (4, b"flag"), (4, b"ping")], maxstack=3))

# An open CALL tail extends fixed table-list values, including nil results,
# and SETLIST must retain the register-to-array index offset.
open(sys.argv[32], "wb").write(build(
    [10, loadk(1, 0), getglobal(2, 1), loadk(3, 2), call(2, 2, 0),
     34 | (1 << 14), ret(0, 2)],
    [(4, b"prefix"), (4, b"produce"), (4, b"payload")], maxstack=4))

open(sys.argv[33], "wb").write(build(
    [10, vararg_open, 34 | (1 << 14), ret(0, 2)], [], maxstack=2, vararg=2))

# A jump can enter an open-argument CALL while bypassing its adjacent producer;
# keep both operations explicit instead of folding across that edge.
open(sys.argv[34], "wb").write(build(
    [getglobal(0, 0), test(0, 0), jmp(2, 6), getglobal(1, 1), loadk(2, 2),
     call(1, 2, 0), call(0, 0, 1), ret(0, 1)],
    [(4, b"flag"), (4, b"produce"), (4, b"payload")], maxstack=3))

# CLOSE detaches an earlier closure. Lua 5.1 ignores JMP's A field, including
# when a TEST controls the jump, so those closures must keep sharing an upvalue.
closure_local_r2 = 36 | (2 << 6)
binding_move_from_r0 = 0
settable_r1_k1_from_r2 = 9 | (1 << 6) | (257 << 23) | (2 << 14)
settable_r1_k3_from_r2 = 9 | (1 << 6) | (259 << 23) | (2 << 14)
getupval_r1 = 4 | (1 << 6)
add_upvalue_and_argument = 12 | (1 << 6) | (1 << 23)
setupval_r1 = 8 | (1 << 6)
ret_r1 = 30 | (1 << 6) | (2 << 23)
mutate_upvalue = [getupval_r1, add_upvalue_and_argument, setupval_r1, getupval_r1, ret_r1]
open(sys.argv[35], "wb").write(build(
    [loadk(0, 0), 10 | (1 << 6), closure_local_r2, binding_move_from_r0, settable_r1_k1_from_r2,
     35, loadk(0, 2), closure_local_r2, binding_move_from_r0,
     settable_r1_k3_from_r2, ret(1, 2)],
    [(3, 1), (3, 1), (3, 2), (3, 2)], maxstack=3,
    children=[dict(code=mutate_upvalue, constants=[], maxstack=2, nups=1, params=1)]))
open(sys.argv[36], "wb").write(build(
    [loadk(0, 0), 10 | (1 << 6), closure_local_r2, binding_move_from_r0, settable_r1_k1_from_r2,
     22 | (1 << 6) | (131071 << 14), loadk(0, 2), closure_local_r2, binding_move_from_r0,
     settable_r1_k3_from_r2, ret(1, 2)],
    [(3, 1), (3, 1), (3, 2), (3, 2)], maxstack=3,
    children=[dict(code=mutate_upvalue, constants=[], maxstack=2, nups=1, params=1)]))
conditional_jump_ignored_a = 22 | (1 << 6) | (131072 << 14)
conditional_jump_code = [
    loadk(0, 0), 10 | (1 << 6), closure_local_r2, binding_move_from_r0,
    settable_r1_k1_from_r2, 5 | (3 << 6) | (3 << 14), 26 | (3 << 6),
    conditional_jump_ignored_a, loadk(0, 2), closure_local_r2, binding_move_from_r0,
    9 | (1 << 6) | (258 << 23) | (2 << 14), 30 | (1 << 6) | (2 << 23),
]
open(sys.argv[37], "wb").write(build(
    conditional_jump_code, [(3, 1), (3, 1), (3, 2), (4, b"flag")], maxstack=4,
    children=[dict(code=mutate_upvalue, constants=[], maxstack=2, nups=1, params=1)]))
open(sys.argv[38], "wb").write(build(
    [22 | (3 << 6) | (131071 << 14), ret(0, 1)], [], maxstack=3))

# Several exits may jump back to one loop header. They mean "continue this
# iteration" in sequence, while a separate forward edge exits the loop.
test_r1_truthy = 26 | (1 << 6) | (1 << 14)
move_r2_r0 = 0 | (2 << 6)
add_r0_one = 12 | (257 << 14)
multi_latch_code = [
    loadk(0, 0), jmp(1, 2), add_r0_one,
    getglobal(1, 2), move_r2_r0, call(1, 2, 2), test_r1_truthy, jmp(7, 2),
    getglobal(1, 3), move_r2_r0, call(1, 2, 2), test_r1_truthy, jmp(12, 2),
    getglobal(1, 4), move_r2_r0, call(1, 2, 2), test_r1_truthy, jmp(17, 19),
    jmp(18, 2), 0 | (1 << 6), ret(1, 2),
]
open(sys.argv[39], "wb").write(build(
    multi_latch_code,
    [(3, 0), (3, 1), (4, b"continueA"), (4, b"continueB"), (4, b"stop")],
    maxstack=3))

# A continue jumps to TFORLOOP, while a separate edge exits the actual for.
# The lifter must keep these meanings distinct inside its synthetic wrapper.
loadnil_r1_r2 = 3 | (1 << 6) | (2 << 23)
move_r5_r3 = 0 | (5 << 6) | (3 << 23)
eq_r3_k0 = 23 | (1 << 6) | (3 << 23) | (256 << 14)
eq_r3_k1 = 23 | (1 << 6) | (3 << 23) | (257 << 14)
tforloop_a0_c1 = 33 | (1 << 14)
generic_for_continue_code = [
    getglobal(0, 2), loadnil_r1_r2, jmp(2, 14),
    getglobal(4, 3), move_r5_r3, call(4, 2, 1),
    eq_r3_k0, jmp(7, 14), eq_r3_k1, jmp(9, 16),
    getglobal(4, 4), move_r5_r3, call(4, 2, 1), jmp(13, 14),
    tforloop_a0_c1, jmp(15, 3), ret(0, 1),
]
open(sys.argv[40], "wb").write(build(
    generic_for_continue_code,
    [(3, 2), (3, 3), (4, b"iterator"), (4, b"seenValue"), (4, b"processValue")],
    maxstack=6))

# Numeric-for continue edges may target CLOSE before FORLOOP; preserve that
# latch path while keeping the separate outer-break edge.
def loop_edge(op, a, pc, target):
    return op | (a << 6) | ((131071 + target - pc - 1) << 14)

eq_r3_k3 = 23 | (1 << 6) | (3 << 23) | ((256 + 3) << 14)
eq_r3_k4 = 23 | (1 << 6) | (3 << 23) | ((256 + 4) << 14)
numeric_for_continue_code = [
    loadk(0, 0), loadk(1, 1), loadk(2, 2), loop_edge(32, 0, 3, 16),
    getglobal(4, 5), move_r5_r3, call(4, 2, 1),
    eq_r3_k3, jmp(8, 15), eq_r3_k4, jmp(10, 17),
    getglobal(4, 6), move_r5_r3, call(4, 2, 1), jmp(14, 15),
    35 | (3 << 6), loop_edge(31, 0, 16, 4), ret(0, 1),
]
open(sys.argv[41], "wb").write(build(
    numeric_for_continue_code,
    [(3, 1), (3, 4), (3, 1), (3, 2), (3, 3), (4, b"seenValue"), (4, b"processValue")],
    maxstack=6))

# The parent `if` range ends just before the generic-for back-jump. That jump
# still proves the TFORLOOP's body entry and must not prevent loop recovery.
test_enabled_r6 = 26 | (6 << 6)
generic_for_nested_if_code = [
    getglobal(6, 0), test_enabled_r6, jmp(2, 11),
    getglobal(0, 1), loadnil_r1_r2, jmp(5, 9),
    getglobal(4, 2), move_r5_r3, call(4, 2, 1),
    tforloop_a0_c1, jmp(10, 6), ret(0, 1),
]
open(sys.argv[42], "wb").write(build(
    generic_for_nested_if_code,
    [(4, b"enabled"), (4, b"iterator"), (4, b"seenValue")],
    maxstack=7))

# A single unconditional latch after a prologue is a closed while loop when
# its only forward exit is the matching break edge.
add_r0_one = 12 | (257 << 14)
eq_r0_three = 23 | (1 << 6) | (258 << 14)
single_latch_loop_code = [
    loadk(0, 0), add_r0_one, eq_r0_three, jmp(3, 5), jmp(4, 1), ret(0, 2),
]
open(sys.argv[43], "wb").write(build(
    single_latch_loop_code, [(3, 0), (3, 1), (3, 3)], maxstack=1))

# A short-circuit candidate checks a gate, fetches a field only when the gate
# is truthy, and jumps to a shared join only when that field is truthy. A
# second candidate follows the same shape; no match assigns a fallback.
test_c0_r0 = 26 | (0 << 6)
test_c1_r1 = 26 | (1 << 6) | (1 << 14)
gettable_r1_r1_r2 = 6 | (1 << 6) | (2 << 14) | (1 << 23)
guarded_short_circuit_code = [
    getglobal(0, 0), test_c0_r0, jmp(2, 8),
    getglobal(1, 1), loadk(2, 6), gettable_r1_r1_r2,
    test_c1_r1, jmp(7, 17),
    getglobal(0, 2), test_c0_r0, jmp(10, 16),
    getglobal(1, 3), loadk(2, 6), gettable_r1_r1_r2,
    test_c1_r1, jmp(15, 17),
    getglobal(1, 4), getglobal(3, 5), 0 | (4 << 6) | (1 << 23),
    call(3, 2, 1), ret(0, 1),
]
open(sys.argv[44], "wb").write(build(
    guarded_short_circuit_code,
    [(4, b"gate1"), (4, b"candidate1"), (4, b"gate2"),
     (4, b"candidate2"), (4, b"fallback"), (4, b"observe"),
     (4, b"available")],
    maxstack=5))

guarded_single_short_circuit_code = [
    getglobal(0, 0), test_c0_r0, jmp(2, 8),
    getglobal(1, 1), loadk(2, 6), gettable_r1_r1_r2,
    test_c1_r1, jmp(7, 9),
    getglobal(1, 4), getglobal(3, 5), 0 | (4 << 6) | (1 << 23),
    call(3, 2, 1), ret(0, 1),
]
open(sys.argv[45], "wb").write(build(
    guarded_single_short_circuit_code,
    [(4, b"gate1"), (4, b"candidate1"), (4, b"gate2"),
     (4, b"candidate2"), (4, b"fallback"), (4, b"observe"),
     (4, b"available")],
    maxstack=5))

# A nested condition can jump straight to the outer else block.  Keep that
# shared fallback in structured source rather than forcing a PC dispatcher.
nested_shared_else_code = [
    getglobal(0, 0), test(0, 0), jmp(2, 9),
    getglobal(1, 1), test(1, 0), jmp(5, 9),
    getglobal(2, 2), call(2, 1, 1), jmp(8, 11),
    getglobal(2, 3), call(2, 1, 1), ret(0, 1),
]
open(sys.argv[46], "wb").write(build(
    nested_shared_else_code,
    [(4, b"outer"), (4, b"inner"), (4, b"body"), (4, b"fallback")],
    maxstack=3))

# One outer latch contains a second loop whose back edge stays inside the
# outer range. Both levels can be expressed with nested while statements.
nested_loop_code = [
    getglobal(0, 0), call(0, 1, 2), test(0, 0), jmp(3, 14),
    getglobal(1, 1), call(1, 1, 2), test(1, 0), jmp(7, 11),
    getglobal(2, 2), call(2, 1, 1), jmp(10, 4),
    getglobal(2, 3), call(2, 1, 1), jmp(13, 0), ret(0, 1),
]
open(sys.argv[47], "wb").write(build(
    nested_loop_code,
    [(4, b"outerGate"), (4, b"innerGate"), (4, b"innerBody"), (4, b"outerBody")],
    maxstack=3))

# A guard chain shares one short failure return, while the last comparison
# jumps around it on success. Keep each check lazy, including setup between
# tests, and preserve the final return value.
shared_return_guards = [
    getglobal(0, 0), 26, jmp(2, 10),
    getglobal(1, 1), 26 | (1 << 6), jmp(5, 10),
    getglobal(3, 2), 6 | (2 << 6) | (3 << 23) | ((256 + 3) << 14),
    23 | (1 << 6) | (2 << 23) | ((256 + 4) << 14), jmp(9, 15),
    getglobal(4, 5), loadk(5, 6), call(4, 2, 1), loadk(4, 9), ret(4, 2),
    getglobal(4, 7), loadk(5, 8), call(4, 2, 2), ret(4, 2),
]
open(sys.argv[48], "wb").write(build(
    shared_return_guards,
    [(4, b"guard1"), (4, b"guard2"), (4, b"status"), (4, b"StatusCode"),
     (3, 200), (4, b"warn"), (4, b"rejected"), (4, b"observe"), (4, b"decoded"), (3, 1000)],
    maxstack=6))

# A then-arm branch can skip a straight-line else arm and continue at their
# shared tail. The else side effect must stay exclusive to the outer false path.
shared_else_join = [
    getglobal(0, 0), 26, jmp(2, 9),
    getglobal(1, 1), call(1, 1, 2), 26 | (1 << 6), jmp(6, 13),
    2 | (2 << 6) | (1 << 23), ret(2, 2),
    getglobal(3, 2), call(3, 1, 1), loadk(3, 3), 0 | (3 << 6) | (3 << 23),
    getglobal(4, 4), call(4, 1, 2), ret(4, 2),
]
open(sys.argv[49], "wb").write(build(
    shared_else_join,
    [(4, b"gate"), (4, b"compare"), (4, b"onElse"), (4, b"unused"), (4, b"observe")],
    maxstack=5))

# Consecutive tests can all branch around one successful return body. The
# comparison setup and body call run only when every guard passes.
shared_body_guards = [
    getglobal(0, 0), 26, jmp(2, 13),
    getglobal(1, 1), call(1, 1, 2), 26 | (1 << 6), jmp(6, 13),
    getglobal(2, 2), 26 | (2 << 6), jmp(9, 13),
    getglobal(3, 3), call(3, 1, 2), ret(3, 2),
    getglobal(3, 4), call(3, 1, 2), ret(3, 2),
]
open(sys.argv[50], "wb").write(build(
    shared_body_guards,
    [(4, b"guard1"), (4, b"prepare"), (4, b"guard2"), (4, b"observe"), (4, b"fallback")],
    maxstack=4))
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
grep -q '^while true do end$' "$TMP/cyclic-jump.lua"
if grep -q 'PC dispatcher preserves Lua 5.1 control flow' "$TMP/cyclic-jump.lua"; then
    echo "a direct self-jump still uses a PC dispatcher" >&2
    exit 1
fi
grep -q '^local r0$' "$TMP/cyclic-jump.lua"
if grep -q '__byteveil_f0_r0' "$TMP/cyclic-jump.lua"; then
    echo "a function without upvalues used a qualified dispatcher register name" >&2
    exit 1
fi
if [[ -n "${BYTEVEIL_LUAC51:-}" ]]; then
    "$BYTEVEIL_LUAC51" -p "$TMP/cyclic-jump.lua"
fi
"$BIN" --bytecode "$TMP/infinite-loop.luac" --format lua >"$TMP/infinite-loop.lua"
grep -q '^while true do$' "$TMP/infinite-loop.lua"
if grep -q 'stopped at repeated control-flow' "$TMP/infinite-loop.lua"; then
    echo "closed unconditional loop was not structurally reconstructed" >&2
    exit 1
fi
"$BIN" --bytecode "$TMP/single-latch-loop.luac" --format lua >"$TMP/single-latch-loop.lua"
grep -q '^r0 = 0$' "$TMP/single-latch-loop.lua"
grep -q '^while true do$' "$TMP/single-latch-loop.lua"
grep -Fq 'if r0 == 3 then' "$TMP/single-latch-loop.lua"
if grep -q 'PC dispatcher\|stopped at repeated control-flow' "$TMP/single-latch-loop.lua"; then
    echo "single-latch loop after a prologue did not reconstruct structurally" >&2
    exit 1
fi
if [[ -n "${BYTEVEIL_LUAC51:-}" ]]; then
    "$BYTEVEIL_LUAC51" -p "$TMP/single-latch-loop.lua"
fi
if [[ -n "${BYTEVEIL_LUA51:-}" ]]; then
    SINGLE_LATCH_PATH="$TMP/single-latch-loop.lua"
    if command -v cygpath >/dev/null 2>&1; then
        SINGLE_LATCH_PATH="$(cygpath -m "$SINGLE_LATCH_PATH")"
    fi
    "$BYTEVEIL_LUA51" "$ROOT/tests/lua51_single_latch_runtime.lua" "$SINGLE_LATCH_PATH"
fi
"$BIN" --bytecode "$TMP/guarded-short-circuit.luac" --format lua >"$TMP/guarded-short-circuit.lua"
grep -q 'local __byteveil_condition_f0_pc1 = false' "$TMP/guarded-short-circuit.lua"
if grep -q 'PC dispatcher preserves Lua 5.1 control flow' "$TMP/guarded-short-circuit.lua"; then
    echo "guarded short-circuit chain still requires a PC dispatcher" >&2
    exit 1
fi
if [[ -n "${BYTEVEIL_LUAC51:-}" ]]; then
    "$BYTEVEIL_LUAC51" -p "$TMP/guarded-short-circuit.lua"
fi
"$BIN" --bytecode "$TMP/guarded-single-short-circuit.luac" --format lua >"$TMP/guarded-single-short-circuit.lua"
grep -q 'local __byteveil_condition_f0_pc1 = false' "$TMP/guarded-single-short-circuit.lua"
if grep -q 'PC dispatcher preserves Lua 5.1 control flow' "$TMP/guarded-single-short-circuit.lua"; then
    echo "single guarded short-circuit chain still requires a PC dispatcher" >&2
    exit 1
fi
if [[ -n "${BYTEVEIL_LUAC51:-}" ]]; then
    "$BYTEVEIL_LUAC51" -p "$TMP/guarded-single-short-circuit.lua"
fi
if [[ -n "${BYTEVEIL_LUA51:-}" ]]; then
    GUARDED_SHORT_CIRCUIT_PATH="$TMP/guarded-short-circuit.lua"
    GUARDED_SINGLE_SHORT_CIRCUIT_PATH="$TMP/guarded-single-short-circuit.lua"
    if command -v cygpath >/dev/null 2>&1; then
        GUARDED_SHORT_CIRCUIT_PATH="$(cygpath -m "$GUARDED_SHORT_CIRCUIT_PATH")"
        GUARDED_SINGLE_SHORT_CIRCUIT_PATH="$(cygpath -m "$GUARDED_SINGLE_SHORT_CIRCUIT_PATH")"
    fi
    "$BYTEVEIL_LUA51" "$ROOT/tests/lua51_guarded_short_circuit_runtime.lua" "$GUARDED_SHORT_CIRCUIT_PATH" "$GUARDED_SINGLE_SHORT_CIRCUIT_PATH"
fi
"$BIN" --bytecode "$TMP/multi-latch-loop.luac" --format lua >"$TMP/multi-latch-loop.lua"
grep -q '^while true do$' "$TMP/multi-latch-loop.lua"
if grep -q 'PC dispatcher\|stopped at repeated control-flow' "$TMP/multi-latch-loop.lua"; then
    echo "multi-latch loop did not reconstruct as structured source" >&2
    exit 1
fi
if [[ -n "${BYTEVEIL_LUAC51:-}" ]]; then
    "$BYTEVEIL_LUAC51" -p "$TMP/multi-latch-loop.lua"
fi
if [[ -n "${BYTEVEIL_LUA51:-}" ]]; then
    MULTI_LATCH_PATH="$TMP/multi-latch-loop.lua"
    if command -v cygpath >/dev/null 2>&1; then
        MULTI_LATCH_PATH="$(cygpath -m "$MULTI_LATCH_PATH")"
    fi
    "$BYTEVEIL_LUA51" "$ROOT/tests/lua51_multi_latch_runtime.lua" "$MULTI_LATCH_PATH"
fi
"$BIN" --bytecode "$TMP/generic-for-continue.luac" --format lua >"$TMP/generic-for-continue.lua"
grep -q '^for r3 in r0, r1, r2 do$' "$TMP/generic-for-continue.lua"
grep -q 'repeat' "$TMP/generic-for-continue.lua"
if grep -q 'PC dispatcher\|stopped at repeated control-flow' "$TMP/generic-for-continue.lua"; then
    echo "generic-for continue and break edges were not reconstructed structurally" >&2
    exit 1
fi
if [[ -n "${BYTEVEIL_LUAC51:-}" ]]; then
    "$BYTEVEIL_LUAC51" -p "$TMP/generic-for-continue.lua"
fi
if [[ -n "${BYTEVEIL_LUA51:-}" ]]; then
    GENERIC_FOR_CONTINUE_PATH="$TMP/generic-for-continue.lua"
    if command -v cygpath >/dev/null 2>&1; then
        GENERIC_FOR_CONTINUE_PATH="$(cygpath -m "$GENERIC_FOR_CONTINUE_PATH")"
    fi
    "$BYTEVEIL_LUA51" "$ROOT/tests/lua51_generic_for_continue_runtime.lua" "$GENERIC_FOR_CONTINUE_PATH"
fi
"$BIN" --bytecode "$TMP/numeric-for-continue.luac" --format lua >"$TMP/numeric-for-continue.lua"
grep -q '^for r3 = 1, 4, 1 do$' "$TMP/numeric-for-continue.lua"
grep -q 'repeat' "$TMP/numeric-for-continue.lua"
if grep -q 'PC dispatcher\|stopped at repeated control-flow' "$TMP/numeric-for-continue.lua"; then
    echo "numeric-for continue and break edges were not reconstructed structurally" >&2
    exit 1
fi
if [[ -n "${BYTEVEIL_LUAC51:-}" ]]; then
    "$BYTEVEIL_LUAC51" -p "$TMP/numeric-for-continue.lua"
fi
if [[ -n "${BYTEVEIL_LUA51:-}" ]]; then
    NUMERIC_FOR_CONTINUE_PATH="$TMP/numeric-for-continue.lua"
    if command -v cygpath >/dev/null 2>&1; then
        NUMERIC_FOR_CONTINUE_PATH="$(cygpath -m "$NUMERIC_FOR_CONTINUE_PATH")"
    fi
    "$BYTEVEIL_LUA51" "$ROOT/tests/lua51_generic_for_continue_runtime.lua" "$NUMERIC_FOR_CONTINUE_PATH"
fi
"$BIN" --bytecode "$TMP/generic-for-nested-if.luac" --format lua >"$TMP/generic-for-nested-if.lua"
grep -q '^if r6 then$' "$TMP/generic-for-nested-if.lua"
grep -q 'for r3 in r0, r1, r2 do' "$TMP/generic-for-nested-if.lua"
if grep -q 'PC dispatcher\|stopped at repeated control-flow\|TFORLOOP at pc' "$TMP/generic-for-nested-if.lua"; then
    echo "generic for at an enclosing branch boundary was not reconstructed structurally" >&2
    exit 1
fi
if [[ -n "${BYTEVEIL_LUAC51:-}" ]]; then
    "$BYTEVEIL_LUAC51" -p "$TMP/generic-for-nested-if.lua"
fi
if [[ -n "${BYTEVEIL_LUA51:-}" ]]; then
    GENERIC_FOR_NESTED_IF_PATH="$TMP/generic-for-nested-if.lua"
    if command -v cygpath >/dev/null 2>&1; then
        GENERIC_FOR_NESTED_IF_PATH="$(cygpath -m "$GENERIC_FOR_NESTED_IF_PATH")"
    fi
    "$BYTEVEIL_LUA51" "$ROOT/tests/lua51_generic_for_nested_if_runtime.lua" "$GENERIC_FOR_NESTED_IF_PATH"
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
"$BIN" --bytecode "$TMP/testset-and.luac" --format lua >"$TMP/testset-and.lua"
"$BIN" --bytecode "$TMP/testset-or.luac" --format lua >"$TMP/testset-or.lua"
grep -q '^if not (r1) then$' "$TMP/testset-and.lua"
grep -q '^if r1 then$' "$TMP/testset-or.lua"
for case in testset-and testset-or; do
    grep -q '^    r0 = r1$' "$TMP/$case.lua"
    grep -q '^else$' "$TMP/$case.lua"
    grep -q '^    r0 = "fallback"$' "$TMP/$case.lua"
    grep -q '^return r0$' "$TMP/$case.lua"
    if grep -q 'TESTSET at pc' "$TMP/$case.lua"; then
        echo "paired TESTSET remained a diagnostic" >&2
        exit 1
    fi
done
"$BIN" --bytecode "$TMP/move-overwritten-source.luac" --format lua >"$TMP/move-overwritten-source.lua"
grep -Fq 'r0(r1)' "$TMP/move-overwritten-source.lua"
if grep -Fq 'r0(r0)' "$TMP/move-overwritten-source.lua"; then
    echo "MOVE copy was rewritten after its source register changed" >&2
    exit 1
fi
"$BIN" --bytecode "$TMP/eq-a1.luac" --format lua >"$TMP/eq-a1.lua"
"$BIN" --bytecode "$TMP/eq-a0.luac" --format lua >"$TMP/eq-a0.lua"
grep -Fq 'if not (r0 == r1) then' "$TMP/eq-a1.lua"
grep -Fq 'if (r0 == r1) then' "$TMP/eq-a0.lua"
"$BIN" --bytecode "$TMP/branch-range-escape.luac" --format lua >"$TMP/branch-range-escape.lua"
grep -q 'PC dispatcher preserves Lua 5.1 control flow in function 0' "$TMP/branch-range-escape.lua"
if grep -Fq 'branch at pc ' "$TMP/branch-range-escape.lua"; then
    echo "PC dispatcher retained a lost structured-branch marker" >&2
    exit 1
fi
"$BIN" --bytecode "$TMP/open-call-chain.luac" --format lua >"$TMP/open-call-chain.lua"
grep -Fq 'r0("fixed", r2("payload"))' "$TMP/open-call-chain.lua"
if grep -q 'open results not consumed' "$TMP/open-call-chain.lua" || grep -q 'unresolved open arguments' "$TMP/open-call-chain.lua"; then
    echo "nested open CALL was not reconstructed" >&2
    exit 1
fi
"$BIN" --bytecode "$TMP/open-vararg-call.luac" --format lua >"$TMP/open-vararg-call.lua"
grep -Fq 'r0(...)' "$TMP/open-vararg-call.lua"
if grep -q 'open results not consumed' "$TMP/open-vararg-call.lua" || grep -q 'unresolved open arguments' "$TMP/open-vararg-call.lua"; then
    echo "open VARARG was not reconstructed as a full argument tail" >&2
    exit 1
fi
"$BIN" --bytecode "$TMP/open-return-call.luac" --format lua >"$TMP/open-return-call.lua"
grep -Fq 'return r0("payload")' "$TMP/open-return-call.lua"
if grep -q 'open results not consumed' "$TMP/open-return-call.lua" || grep -q 'unresolved open result tail' "$TMP/open-return-call.lua"; then
    echo "open CALL results were not reconstructed by RETURN" >&2
    exit 1
fi
"$BIN" --bytecode "$TMP/open-tailcall.luac" --format lua >"$TMP/open-tailcall.lua"
grep -Fq 'return r0(...)' "$TMP/open-tailcall.lua"
if grep -q 'unresolved open arguments' "$TMP/open-tailcall.lua"; then
    echo "open TAILCALL arguments were not reconstructed" >&2
    exit 1
fi
"$BIN" --bytecode "$TMP/colon-self-call.luac" --format lua >"$TMP/colon-self-call.lua"
grep -Fq 'r1:ping()' "$TMP/colon-self-call.lua"
if grep -Fq 'r0 = r1["ping"]' "$TMP/colon-self-call.lua" || grep -Fq 'r1 = r1' "$TMP/colon-self-call.lua"; then
    echo "fused colon call still emits SELF temporaries" >&2
    exit 1
fi
"$BIN" --bytecode "$TMP/colon-open-call.luac" --format lua >"$TMP/colon-open-call.lua"
grep -Fq 'r0(r2:ping())' "$TMP/colon-open-call.lua"
if grep -Fq 'r1 = r2["ping"]' "$TMP/colon-open-call.lua" || grep -Fq 'open results not consumed' "$TMP/colon-open-call.lua"; then
    echo "open method results were not folded into their consumer" >&2
    exit 1
fi
"$BIN" --bytecode "$TMP/nested-branch-exit.luac" --format lua >"$TMP/nested-branch-exit.lua"
if grep -Fq 'PC dispatcher preserves Lua 5.1 control flow' "$TMP/nested-branch-exit.lua"; then
    echo "nested conditional with a shared return was not reconstructed structurally" >&2
    exit 1
fi
grep -Fq 'if r0 then' "$TMP/nested-branch-exit.lua"
grep -Fq 'if r2 then' "$TMP/nested-branch-exit.lua"
if grep -Fq 'branch at pc ' "$TMP/nested-branch-exit.lua"; then
    echo "structured shared-join branch was left as a diagnostic" >&2
    exit 1
fi
if grep -Fq 'RETURN at pc 13 has an unresolved open result tail' "$TMP/nested-branch-exit.lua"; then
    echo "the return after a terminal TAILCALL was emitted as reachable" >&2
    exit 1
fi
"$BIN" --bytecode "$TMP/nested-shared-else.luac" --format lua >"$TMP/nested-shared-else.lua"
if grep -Fq 'PC dispatcher preserves Lua 5.1 control flow' "$TMP/nested-shared-else.lua"; then
    echo "nested branch to a shared outer else still needs a PC dispatcher" >&2
    exit 1
fi
grep -Fq 'if r0 then' "$TMP/nested-shared-else.lua"
grep -Fq 'if r1 then' "$TMP/nested-shared-else.lua"
if [[ -n "${BYTEVEIL_LUA51:-}" ]]; then
    NESTED_SHARED_ELSE_BYTECODE="$TMP/nested-shared-else.luac"
    NESTED_SHARED_ELSE_LUA="$TMP/nested-shared-else.lua"
    if command -v cygpath >/dev/null 2>&1; then
        NESTED_SHARED_ELSE_BYTECODE="$(cygpath -m "$NESTED_SHARED_ELSE_BYTECODE")"
        NESTED_SHARED_ELSE_LUA="$(cygpath -m "$NESTED_SHARED_ELSE_LUA")"
    fi
    "$BYTEVEIL_LUA51" "$ROOT/tests/lua51_nested_shared_else_runtime.lua" "$NESTED_SHARED_ELSE_BYTECODE" "$NESTED_SHARED_ELSE_LUA"
fi
"$BIN" --bytecode "$TMP/nested-loops.luac" --format lua >"$TMP/nested-loops.lua"
if grep -Fq 'PC dispatcher preserves Lua 5.1 control flow' "$TMP/nested-loops.lua"; then
    echo "nested while loops still require a PC dispatcher" >&2
    exit 1
fi
if [[ -n "${BYTEVEIL_LUAC51:-}" ]]; then
    "$BYTEVEIL_LUAC51" -p "$TMP/nested-loops.lua"
fi
if [[ -n "${BYTEVEIL_LUA51:-}" ]]; then
    NESTED_LOOPS_BYTECODE="$TMP/nested-loops.luac"
    NESTED_LOOPS_LUA="$TMP/nested-loops.lua"
    if command -v cygpath >/dev/null 2>&1; then
        NESTED_LOOPS_BYTECODE="$(cygpath -m "$NESTED_LOOPS_BYTECODE")"
        NESTED_LOOPS_LUA="$(cygpath -m "$NESTED_LOOPS_LUA")"
    fi
    "$BYTEVEIL_LUA51" "$ROOT/tests/lua51_nested_loops_runtime.lua" "$NESTED_LOOPS_BYTECODE" "$NESTED_LOOPS_LUA"
fi
"$BIN" --bytecode "$TMP/shared-return-guards.luac" --format lua >"$TMP/shared-return-guards.lua"
if grep -Fq 'PC dispatcher preserves Lua 5.1 control flow' "$TMP/shared-return-guards.lua"; then
    echo "shared early-return guard chain still requires a PC dispatcher" >&2
    exit 1
fi
if [[ -n "${BYTEVEIL_LUAC51:-}" ]]; then
    "$BYTEVEIL_LUAC51" -p "$TMP/shared-return-guards.lua"
fi
if [[ -n "${BYTEVEIL_LUA51:-}" ]]; then
    SHARED_RETURN_GUARDS_BYTECODE="$TMP/shared-return-guards.luac"
    SHARED_RETURN_GUARDS_LUA="$TMP/shared-return-guards.lua"
    if command -v cygpath >/dev/null 2>&1; then
        SHARED_RETURN_GUARDS_BYTECODE="$(cygpath -m "$SHARED_RETURN_GUARDS_BYTECODE")"
        SHARED_RETURN_GUARDS_LUA="$(cygpath -m "$SHARED_RETURN_GUARDS_LUA")"
    fi
    "$BYTEVEIL_LUA51" "$ROOT/tests/lua51_shared_return_guard_runtime.lua" \
        "$SHARED_RETURN_GUARDS_BYTECODE" "$SHARED_RETURN_GUARDS_LUA"
fi
"$BIN" --bytecode "$TMP/shared-else-join.luac" --format lua >"$TMP/shared-else-join.lua"
if grep -Fq 'PC dispatcher preserves Lua 5.1 control flow' "$TMP/shared-else-join.lua"; then
    echo "branch to the shared tail after an else block still needs a PC dispatcher" >&2
    exit 1
fi
if [[ -n "${BYTEVEIL_LUAC51:-}" ]]; then
    "$BYTEVEIL_LUAC51" -p "$TMP/shared-else-join.lua"
fi
if [[ -n "${BYTEVEIL_LUA51:-}" ]]; then
    SHARED_ELSE_JOIN_BYTECODE="$TMP/shared-else-join.luac"
    SHARED_ELSE_JOIN_LUA="$TMP/shared-else-join.lua"
    if command -v cygpath >/dev/null 2>&1; then
        SHARED_ELSE_JOIN_BYTECODE="$(cygpath -m "$SHARED_ELSE_JOIN_BYTECODE")"
        SHARED_ELSE_JOIN_LUA="$(cygpath -m "$SHARED_ELSE_JOIN_LUA")"
    fi
    "$BYTEVEIL_LUA51" "$ROOT/tests/lua51_shared_else_join_runtime.lua" \
        "$SHARED_ELSE_JOIN_BYTECODE" "$SHARED_ELSE_JOIN_LUA"
fi
"$BIN" --bytecode "$TMP/shared-body-guards.luac" --format lua >"$TMP/shared-body-guards.lua"
if grep -Fq 'PC dispatcher preserves Lua 5.1 control flow' "$TMP/shared-body-guards.lua"; then
    echo "shared returning guard body still requires a PC dispatcher" >&2
    exit 1
fi
if [[ -n "${BYTEVEIL_LUAC51:-}" ]]; then
    "$BYTEVEIL_LUAC51" -p "$TMP/shared-body-guards.lua"
fi
if [[ -n "${BYTEVEIL_LUA51:-}" ]]; then
    SHARED_BODY_GUARDS_BYTECODE="$TMP/shared-body-guards.luac"
    SHARED_BODY_GUARDS_LUA="$TMP/shared-body-guards.lua"
    if command -v cygpath >/dev/null 2>&1; then
        SHARED_BODY_GUARDS_BYTECODE="$(cygpath -m "$SHARED_BODY_GUARDS_BYTECODE")"
        SHARED_BODY_GUARDS_LUA="$(cygpath -m "$SHARED_BODY_GUARDS_LUA")"
    fi
    "$BYTEVEIL_LUA51" "$ROOT/tests/lua51_shared_body_guard_runtime.lua" \
        "$SHARED_BODY_GUARDS_BYTECODE" "$SHARED_BODY_GUARDS_LUA"
fi
"$BIN" --bytecode "$TMP/colon-flow-entry.luac" --format lua >"$TMP/colon-flow-entry.lua"
if grep -Fq ':ping(' "$TMP/colon-flow-entry.lua"; then
    echo "a branch that skips SELF was incorrectly rendered as a colon call" >&2
    exit 1
fi
"$BIN" --bytecode "$TMP/open-setlist.luac" --format lua >"$TMP/open-setlist.lua"
if grep -q 'has an open value tail' "$TMP/open-setlist.lua"; then
    echo "adjacent open results were dropped by SETLIST" >&2
    exit 1
fi
"$BIN" --bytecode "$TMP/open-setlist-vararg.luac" --format lua >"$TMP/open-setlist-vararg.lua"
if grep -q 'has an open value tail' "$TMP/open-setlist-vararg.lua"; then
    echo "open VARARG results were dropped by SETLIST" >&2
    exit 1
fi
"$BIN" --bytecode "$TMP/open-branch-entry.luac" --format lua >"$TMP/open-branch-entry.lua"
grep -Fq 'has open results not consumed' "$TMP/open-branch-entry.lua"
grep -Fq 'has unresolved open arguments' "$TMP/open-branch-entry.lua"
if grep -Fq 'r0(r1("payload"))' "$TMP/open-branch-entry.lua"; then
    echo "open results were folded across a branch that bypasses the producer" >&2
    exit 1
fi
"$BIN" --bytecode "$TMP/close-captured-register.luac" --format lua >"$TMP/close-captured-register.lua"
"$BIN" --bytecode "$TMP/jump-a-ignored-captured-register.luac" --format lua >"$TMP/jump-a-ignored-captured-register.lua"
"$BIN" --bytecode "$TMP/conditional-jump-a-ignored-captured-register.luac" --format lua >"$TMP/conditional-jump-a-ignored-captured-register.lua"
if [[ -n "${BYTEVEIL_LUA51:-}" ]]; then
    MOVE_COPY_PATH="$TMP/move-overwritten-source.lua"
    EQ_A1_PATH="$TMP/eq-a1.lua"
    EQ_A0_PATH="$TMP/eq-a0.lua"
    OPEN_CALL_PATH="$TMP/open-call-chain.lua"
    OPEN_VARARG_PATH="$TMP/open-vararg-call.lua"
    OPEN_RETURN_PATH="$TMP/open-return-call.lua"
    OPEN_TAILCALL_PATH="$TMP/open-tailcall.lua"
    COLON_SELF_PATH="$TMP/colon-self-call.lua"
    COLON_OPEN_PATH="$TMP/colon-open-call.lua"
    NESTED_BRANCH_PATH="$TMP/nested-branch-exit.lua"
    COLON_FLOW_PATH="$TMP/colon-flow-entry.lua"
    OPEN_SETLIST_PATH="$TMP/open-setlist.lua"
    OPEN_SETLIST_VARARG_PATH="$TMP/open-setlist-vararg.lua"
    CLOSE_CAPTURED_PATH="$TMP/close-captured-register.lua"
    CLOSE_CAPTURED_BYTECODE_PATH="$TMP/close-captured-register.luac"
    JMP_A_IGNORED_PATH="$TMP/jump-a-ignored-captured-register.lua"
    JMP_A_IGNORED_BYTECODE_PATH="$TMP/jump-a-ignored-captured-register.luac"
    CONDITIONAL_JMP_A_IGNORED_PATH="$TMP/conditional-jump-a-ignored-captured-register.lua"
    CONDITIONAL_JMP_A_IGNORED_BYTECODE_PATH="$TMP/conditional-jump-a-ignored-captured-register.luac"
    CLOSURE_RUNTIME_SCRIPT="$ROOT/tests/lua51_closure_runtime.lua"
    if command -v cygpath >/dev/null 2>&1; then
        MOVE_COPY_PATH="$(cygpath -m "$MOVE_COPY_PATH")"
        EQ_A1_PATH="$(cygpath -m "$EQ_A1_PATH")"
        EQ_A0_PATH="$(cygpath -m "$EQ_A0_PATH")"
        OPEN_CALL_PATH="$(cygpath -m "$OPEN_CALL_PATH")"
        OPEN_VARARG_PATH="$(cygpath -m "$OPEN_VARARG_PATH")"
        OPEN_RETURN_PATH="$(cygpath -m "$OPEN_RETURN_PATH")"
        OPEN_TAILCALL_PATH="$(cygpath -m "$OPEN_TAILCALL_PATH")"
        COLON_SELF_PATH="$(cygpath -m "$COLON_SELF_PATH")"
        COLON_OPEN_PATH="$(cygpath -m "$COLON_OPEN_PATH")"
        NESTED_BRANCH_PATH="$(cygpath -m "$NESTED_BRANCH_PATH")"
        COLON_FLOW_PATH="$(cygpath -m "$COLON_FLOW_PATH")"
        OPEN_SETLIST_PATH="$(cygpath -m "$OPEN_SETLIST_PATH")"
        OPEN_SETLIST_VARARG_PATH="$(cygpath -m "$OPEN_SETLIST_VARARG_PATH")"
        CLOSE_CAPTURED_PATH="$(cygpath -m "$CLOSE_CAPTURED_PATH")"
        CLOSE_CAPTURED_BYTECODE_PATH="$(cygpath -m "$CLOSE_CAPTURED_BYTECODE_PATH")"
        JMP_A_IGNORED_PATH="$(cygpath -m "$JMP_A_IGNORED_PATH")"
        JMP_A_IGNORED_BYTECODE_PATH="$(cygpath -m "$JMP_A_IGNORED_BYTECODE_PATH")"
        CONDITIONAL_JMP_A_IGNORED_PATH="$(cygpath -m "$CONDITIONAL_JMP_A_IGNORED_PATH")"
        CONDITIONAL_JMP_A_IGNORED_BYTECODE_PATH="$(cygpath -m "$CONDITIONAL_JMP_A_IGNORED_BYTECODE_PATH")"
        CLOSURE_RUNTIME_SCRIPT="$(cygpath -m "$CLOSURE_RUNTIME_SCRIPT")"
    fi
    "$BYTEVEIL_LUA51" -e "object='saved'; callback=function(value) return value end; local copied=assert(loadfile('$MOVE_COPY_PATH')); assert(copied() == 'saved'); assert(dofile('$EQ_A1_PATH') == 'else'); assert(dofile('$EQ_A0_PATH') == 'then'); captured=nil; produce=function(x) return x..'-one', x..'-two' end; consume=function(...) captured={...} end; assert(dofile('$OPEN_CALL_PATH') == nil); assert(#captured==3 and captured[1]=='fixed' and captured[2]=='payload-one' and captured[3]=='payload-two'); captured=nil; local openvararg=assert(loadfile('$OPEN_VARARG_PATH')); openvararg('alpha','beta'); assert(#captured==2 and captured[1]=='alpha' and captured[2]=='beta'); local openreturn=assert(loadfile('$OPEN_RETURN_PATH')); local first,second=openreturn(); assert(first=='payload-one' and second=='payload-two'); consume=function(...) return ... end; local opentail=assert(loadfile('$OPEN_TAILCALL_PATH')); first,second=opentail('gamma','delta'); assert(first=='gamma' and second=='delta'); ping_called=false; object={ping=function(self) assert(self==object); ping_called=true end}; assert(dofile('$COLON_SELF_PATH') == nil); assert(ping_called); captured=nil; object={ping=function(self) assert(self==object); return 'method-one','method-two' end}; consume=function(...) captured={...} end; assert(dofile('$COLON_OPEN_PATH') == nil); assert(#captured==2 and captured[1]=='method-one' and captured[2]=='method-two'); flag=true; inner=false; assert(dofile('$NESTED_BRANCH_PATH')=='default'); flag=true; inner=true; assert(dofile('$NESTED_BRANCH_PATH')=='left'); flag=false; inner=true; assert(dofile('$NESTED_BRANCH_PATH')=='right'); local plain_calls,method_calls=0,0; plain=function(value) plain_calls=plain_calls+1; return 'plain' end; object={ping=function(self) assert(self==object); method_calls=method_calls+1; return 'method' end}; flag=false; assert(dofile('$COLON_FLOW_PATH')=='plain'); flag=true; assert(dofile('$COLON_FLOW_PATH')=='method'); assert(plain_calls==1 and method_calls==1); produce=function(value) return value..'-head', nil, value..'-tail' end; local listed=dofile('$OPEN_SETLIST_PATH'); assert(listed[1]=='prefix' and listed[2]=='payload-head' and listed[3]==nil and listed[4]=='payload-tail'); local listvararg=assert(loadfile('$OPEN_SETLIST_VARARG_PATH')); local variadic=listvararg('one', nil, 'three'); assert(variadic[1]=='one' and variadic[2]==nil and variadic[3]=='three')"
    "$BYTEVEIL_LUA51" "$CLOSURE_RUNTIME_SCRIPT" "$CLOSE_CAPTURED_PATH" "$CLOSE_CAPTURED_BYTECODE_PATH" "$JMP_A_IGNORED_PATH" "$JMP_A_IGNORED_BYTECODE_PATH" "$CONDITIONAL_JMP_A_IGNORED_PATH" "$CONDITIONAL_JMP_A_IGNORED_BYTECODE_PATH"
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
grep -q '^r1 = function(...)$' "$TMP/closure-local.lua"
grep -q '^    __byteveil_f1_r0 = r0$' "$TMP/closure-local.lua"
if grep -q '^r255 = r0$' "$TMP/closure-local.lua"; then
    echo "CLOSURE local capture was emitted as a standalone MOVE" >&2
    exit 1
fi
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
for case in bad-jump bad-jump-a-register bad-constant bad-register missing-extra closure-truncated closure-invalid-kind closure-invalid-source closure-jump-into-binding; do
    if "$BIN" --bytecode "$TMP/$case.luac" --format json >"$TMP/$case.out" 2>"$TMP/$case.err"; then
        echo "malformed Lua 5.1 case $case was accepted" >&2
        exit 1
    fi
done
grep -q 'jump target is not an instruction boundary' "$TMP/bad-jump.err"
grep -q 'register A out of range' "$TMP/bad-jump-a-register.err"
grep -q 'constant index out of range' "$TMP/bad-constant.err"
grep -q 'register B out of range' "$TMP/bad-register.err"
grep -q 'SETLIST is missing its extra block word' "$TMP/missing-extra.err"
grep -q 'CLOSURE capture bindings truncated' "$TMP/closure-truncated.err"
grep -q 'CLOSURE capture binding must be MOVE or GETUPVAL' "$TMP/closure-invalid-kind.err"
grep -q 'CLOSURE local capture B out of range' "$TMP/closure-invalid-source.err"
grep -q 'jump target is not an instruction boundary' "$TMP/closure-jump-into-binding.err"
printf 'Lua 5.1 reader tests: PASS\n'
