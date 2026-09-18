#!/usr/bin/env bash
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"
byteveil_find_python
BIN="${1:-./build/byteveil}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cat >"$TMP/semantics.luau" <<'LUA'
local state = 0

local function mutate(t, key, value, left, right)
    t[key] = value
    t.answer = value
    published = value
    local result = t[key]
    if left == right then
        result = left
    else
        result = right
    end
    return result
end

local function update(value)
    state = value
    return state
end

local function calls(fn, a, b)
    local x, y = fn(a, b)
    return x, y
end

local function method(object)
    return object:ping(1)
end

local function varargs(...)
    local a, b = ...
    return a, b
end

local function loops(items)
    local total = 0
    for i = 1, 3 do
        total += i
    end
    for _, value in pairs(items) do
        total += value
    end
    return total
end

local function nested(value)
    local function first()
        return value
    end
    local function second()
        local function third()
            return value + 1
        end
        return third()
    end
    return first(), second()
end

return mutate, update, calls, method, varargs, loops, nested
LUA

"$BIN" --format json "$TMP/semantics.luau" >"$TMP/semantics.json"
"$BIN" --format lua "$TMP/semantics.luau" >"$TMP/reconstructed.luau"
grep -q '^-- ByteVeil Luau register-state reconstruction' "$TMP/reconstructed.luau"
grep -q 'local capture_' "$TMP/reconstructed.luau"
grep -q 'cells\[' "$TMP/reconstructed.luau"
grep -q 'iterator_results_' "$TMP/reconstructed.luau"
! grep -q 'unsupported opcode' "$TMP/reconstructed.luau"
"$BYTEVEIL_PYTHON" - "$TMP/semantics.json" <<'PY'
import json
import sys

module = json.load(open(sys.argv[1], encoding="utf-8"))
root = module["root_function"]

functions = []
def walk(function, parent=None):
    functions.append(function)
    if parent is not None:
        assert function["parent_id"] == parent["id"]
    for child in function["children"]:
        walk(child, function)
walk(root)
ids = [function["id"] for function in functions]
assert len(ids) == len(set(ids)), ids
assert ids == list(range(len(ids))), ids

instructions = [instruction for function in functions for instruction in function["instructions"]]
by_name = {}
for instruction in instructions:
    by_name.setdefault(instruction["opcode_name"], []).append(instruction)

settable = by_name["SETTABLE"][0]
assert set(settable["uses"]) == {settable["a"], settable["b"], settable["c"]}
assert not settable["definitions"]

settableks = by_name["SETTABLEKS"][0]
assert set(settableks["uses"]) == {settableks["a"], settableks["b"]}
assert settableks["constant_index"] == settableks["aux"]

setglobal = by_name["SETGLOBAL"][0]
assert setglobal["uses"] == [setglobal["a"]]
assert setglobal["constant_index"] == setglobal["aux"]

setupval = by_name["SETUPVAL"][0]
assert setupval["uses"] == [setupval["a"]]

comparison_names = {"JUMPIFEQ", "JUMPIFLE", "JUMPIFLT", "JUMPIFNOTEQ", "JUMPIFNOTLE", "JUMPIFNOTLT"}
comparison = next(instruction for instruction in instructions if instruction["opcode_name"] in comparison_names)
assert comparison["a"] in comparison["uses"]
assert comparison["aux"] in comparison["uses"]

call = next(instruction for instruction in by_name["CALL"] if instruction["c"] == 3)
assert call["definitions"] == [call["a"], call["a"] + 1]
assert len(call["definitions"]) == len(call["definition_versions"])

namecall = by_name["NAMECALL"][0]
assert namecall["uses"] == [namecall["b"]]
assert namecall["definitions"] == [namecall["a"], namecall["a"] + 1]

getvarargs = by_name["GETVARARGS"][0]
assert getvarargs["definitions"] == [getvarargs["a"], getvarargs["a"] + 1]

for function in functions:
    assert function["dataflow"]["unknown_instructions"] == 0
    blocks = function["basic_blocks"]
    if not blocks:
        continue
    reached = {0}
    pending = [0]
    while pending:
        block_id = pending.pop()
        for successor in blocks[block_id]["successors"]:
            assert block_id in blocks[successor]["predecessors"]
            if successor not in reached:
                reached.add(successor)
                pending.append(successor)
    assert reached == {block["id"] for block in blocks if block["reachable"]}
    for block in blocks:
        for predecessor in block["predecessors"]:
            assert block["id"] in blocks[predecessor]["successors"]
        last = function["instructions"][block["instructions"][-1]]
        if last["jump_target"] < 0 and last["opcode_name"] != "RETURN" and block["id"] + 1 < len(blocks):
            assert block["id"] + 1 in block["successors"], (function["id"], block, last)
        if block["reachable"] and block["id"] != 0:
            assert function["cfg_analysis"]["immediate_dominators"][block["id"]] >= 0

for instruction in by_name.get("FORGPREP", []) + by_name.get("FORGPREP_INEXT", []) + by_name.get("FORGPREP_NEXT", []):
    function = next(function for function in functions if instruction in function["instructions"])
    block = function["basic_blocks"][instruction["source_block"]]
    if function["instructions"][block["instructions"][-1]] is instruction:
        assert block["successors"] == [instruction["target_block"]]

assert any(len(instruction["definitions"]) > 1 for instruction in instructions)
assert all(len(instruction["definitions"]) == len(instruction["definition_versions"]) for instruction in instructions)
PY

printf 'Luau IR semantic tests: PASS\n'
