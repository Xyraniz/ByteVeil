#!/usr/bin/env bash
set -euo pipefail
BIN="${1:-./build/byteveil}"
RUNNER="${2:-./build/byteveil_luau_runner}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cat >"$TMP/original.luau" <<'LUA'
local state = 1

local function increment(value)
    state += value
    return state
end

local function classify(value)
    if value < 0 then
        return "negative"
    elseif value == 0 then
        return "zero"
    end
    return "positive"
end

local values = {2, 4, 6}
local total = 0
for index = 1, 3 do
    total += index
end
for _, value in pairs(values) do
    total += value
end

local object = { value = 5 }
local function multiply(self, value)
    return self.value * value
end
object.multiply = multiply

local function pair(...)
    local first, second = ...
    return first, second
end
local first, second = pair(3, 4)

print(classify(total), total, increment(2), increment(3), object:multiply(first + second))
LUA

"$BIN" --format lua "$TMP/original.luau" >"$TMP/reconstructed.luau"
grep -q '^-- ByteVeil Luau register-state reconstruction' "$TMP/reconstructed.luau"
"$RUNNER" "$TMP/original.luau" >"$TMP/original.out" 2>"$TMP/original.err"
"$RUNNER" "$TMP/reconstructed.luau" >"$TMP/reconstructed.out" 2>"$TMP/reconstructed.err"
cmp "$TMP/original.out" "$TMP/reconstructed.out"
cmp "$TMP/original.err" "$TMP/reconstructed.err"

cat >"$TMP/control_edges.luau" <<'LUA'
local captured = false
local function readCaptured()
    return captured
end

local total = 0
for index = 3, 1, -1 do
    total += index
end

local countdown = 3
while countdown > 0 do
    total += countdown
    countdown -= 1
end

repeat
    total -= 1
until total <= 10

local values = {}
local key = "answer"
values[key] = total
if not readCaptured() and values[key] == 10 then
    print("control", values.answer)
else
    print("unexpected")
end
LUA

"$BIN" --format lua "$TMP/control_edges.luau" >"$TMP/control_edges.reconstructed.luau"
"$RUNNER" "$TMP/control_edges.luau" >"$TMP/control_edges.original.out" 2>"$TMP/control_edges.original.err"
"$RUNNER" "$TMP/control_edges.reconstructed.luau" >"$TMP/control_edges.reconstructed.out" 2>"$TMP/control_edges.reconstructed.err"
cmp "$TMP/control_edges.original.out" "$TMP/control_edges.reconstructed.out"
cmp "$TMP/control_edges.original.err" "$TMP/control_edges.reconstructed.err"

cat >"$TMP/duplicated_closure.luau" <<'LUA'
local prefix = "captured"
local function makeReader(suffix)
    return function()
        return prefix .. suffix
    end
end

local left = makeReader("-left")
local right = makeReader("-right")
print(left(), right())
LUA

"$BIN" --format lua "$TMP/duplicated_closure.luau" >"$TMP/duplicated_closure.reconstructed.luau"
! grep -q 'orphan CAPTURE' "$TMP/duplicated_closure.reconstructed.luau"
"$RUNNER" "$TMP/duplicated_closure.luau" >"$TMP/duplicated_closure.original.out" 2>"$TMP/duplicated_closure.original.err"
"$RUNNER" "$TMP/duplicated_closure.reconstructed.luau" >"$TMP/duplicated_closure.reconstructed.out" 2>"$TMP/duplicated_closure.reconstructed.err"
cmp "$TMP/duplicated_closure.original.out" "$TMP/duplicated_closure.reconstructed.out"
cmp "$TMP/duplicated_closure.original.err" "$TMP/duplicated_closure.reconstructed.err"
printf 'Luau register-state equivalence: PASS\n'
