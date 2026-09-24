#!/usr/bin/env bash
set -euo pipefail
BIN="${1:-./build/byteveil}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"
if ! byteveil_find_lua51; then
    printf 'Lua 5.1 reconstruction tests: SKIP (Lua 5.1 interpreter/compiler not installed)\n'
    exit 0
fi
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cat >"$TMP/control.lua" <<'LUA'
local function classify(value)
    if value < 0 then return "negative"
    elseif value == 0 then return "zero"
    else return "positive" end
end
local total = 0
for i = 1, 3 do total = total + i end
local n = 3
while n > 0 do n = n - 1 end
repeat total = total - 1 until total <= 0
return classify(total)
LUA
"$BYTEVEIL_LUAC51" -o "$TMP/control.luac" "$TMP/control.lua"
"$BIN" --bytecode "$TMP/control.luac" --format lua > "$TMP/reconstructed.lua"
ORIGINAL_PATH="$TMP/control.lua"
RECONSTRUCTED_PATH="$TMP/reconstructed.lua"
if command -v cygpath >/dev/null 2>&1; then
    ORIGINAL_PATH="$(cygpath -m "$ORIGINAL_PATH")"
    RECONSTRUCTED_PATH="$(cygpath -m "$RECONSTRUCTED_PATH")"
fi
"$BYTEVEIL_LUA51" -e "local original=assert(loadfile('$ORIGINAL_PATH')); local reconstructed=assert(loadfile('$RECONSTRUCTED_PATH')); for _, value in ipairs({-1, 0, 1, 3}) do assert(original(value) == reconstructed(value), 'behavior differs for ' .. tostring(value)) end"
grep -q '^for ' "$TMP/reconstructed.lua"
grep -q '^while ' "$TMP/reconstructed.lua"
grep -q '^repeat$' "$TMP/reconstructed.lua"
grep -q 'if ' "$TMP/reconstructed.lua"
grep -q 'elseif\|else' "$TMP/reconstructed.lua"
! grep -q 'unsupported opcode retained' "$TMP/reconstructed.lua"
cat >"$TMP/read-chain.lua" <<'LUA'
local function read(root)
    return root.branch.leaf
end
return read
LUA
"$BYTEVEIL_LUAC51" -o "$TMP/read-chain.luac" "$TMP/read-chain.lua"
"$BIN" --bytecode "$TMP/read-chain.luac" --format lua > "$TMP/read-chain.reconstructed.lua"
grep -Eq '= r0\["branch"\]\["leaf"\]' "$TMP/read-chain.reconstructed.lua"
"$BYTEVEIL_LUAC51" -p "$TMP/read-chain.reconstructed.lua"
READ_CHAIN_ORIGINAL_PATH="$TMP/read-chain.lua"
READ_CHAIN_RECONSTRUCTED_PATH="$TMP/read-chain.reconstructed.lua"
if command -v cygpath >/dev/null 2>&1; then
    READ_CHAIN_ORIGINAL_PATH="$(cygpath -m "$READ_CHAIN_ORIGINAL_PATH")"
    READ_CHAIN_RECONSTRUCTED_PATH="$(cygpath -m "$READ_CHAIN_RECONSTRUCTED_PATH")"
fi
"$BYTEVEIL_LUA51" "$ROOT/tests/lua51_read_chain_runtime.lua" \
    "$READ_CHAIN_ORIGINAL_PATH" "$READ_CHAIN_RECONSTRUCTED_PATH"
cat >"$TMP/method-argument.lua" <<'LUA'
local function invoke(object, argument)
    return object:ping(argument)
end
local function invokeComputed(object, evaluate)
    return object:ping(evaluate())
end
return invoke, invokeComputed
LUA
"$BYTEVEIL_LUAC51" -o "$TMP/method-argument.luac" "$TMP/method-argument.lua"
"$BIN" --bytecode "$TMP/method-argument.luac" --format lua > "$TMP/method-argument.reconstructed.lua"
test "$(grep -Fc ':ping(' "$TMP/method-argument.reconstructed.lua")" -eq 1
grep -Eq 'return r[0-9]+:ping\(r[0-9]+\)' "$TMP/method-argument.reconstructed.lua"
"$BYTEVEIL_LUAC51" -p "$TMP/method-argument.reconstructed.lua"
METHOD_ARGUMENT_ORIGINAL_PATH="$TMP/method-argument.lua"
METHOD_ARGUMENT_RECONSTRUCTED_PATH="$TMP/method-argument.reconstructed.lua"
if command -v cygpath >/dev/null 2>&1; then
    METHOD_ARGUMENT_ORIGINAL_PATH="$(cygpath -m "$METHOD_ARGUMENT_ORIGINAL_PATH")"
    METHOD_ARGUMENT_RECONSTRUCTED_PATH="$(cygpath -m "$METHOD_ARGUMENT_RECONSTRUCTED_PATH")"
fi
"$BYTEVEIL_LUA51" "$ROOT/tests/lua51_method_argument_runtime.lua" \
    "$METHOD_ARGUMENT_ORIGINAL_PATH" "$METHOD_ARGUMENT_RECONSTRUCTED_PATH"
ISOLATED_DISPATCH_FIXTURE="$ROOT/tests/fixtures/lua51-isolated-dispatch.luac"
"$BIN" --bytecode "$ISOLATED_DISPATCH_FIXTURE" --format lua > "$TMP/isolated-dispatch.reconstructed.lua"
grep -q 'isolated PC dispatcher preserves Lua 5.1 control flow' "$TMP/isolated-dispatch.reconstructed.lua"
! grep -q -- '-- ByteVeil: PC dispatcher preserves Lua 5.1 control flow' "$TMP/isolated-dispatch.reconstructed.lua"
! grep -q 'branch at pc\|unsupported opcode retained\|left reconstructed range' "$TMP/isolated-dispatch.reconstructed.lua"
"$BYTEVEIL_LUAC51" -p "$TMP/isolated-dispatch.reconstructed.lua"
ISOLATED_ORIGINAL_PATH="$ROOT/tests/lua51_isolated_dispatch_reference.lua"
ISOLATED_RECONSTRUCTED_PATH="$TMP/isolated-dispatch.reconstructed.lua"
if command -v cygpath >/dev/null 2>&1; then
    ISOLATED_ORIGINAL_PATH="$(cygpath -m "$ISOLATED_ORIGINAL_PATH")"
    ISOLATED_RECONSTRUCTED_PATH="$(cygpath -m "$ISOLATED_RECONSTRUCTED_PATH")"
fi
"$BYTEVEIL_LUA51" "$ROOT/tests/lua51_isolated_dispatch_runtime.lua" \
    "$ISOLATED_ORIGINAL_PATH" "$ISOLATED_RECONSTRUCTED_PATH"
cat >"$TMP/if-loop.lua" <<'LUA'
local function bounded(enabled, limit)
    local total = 0
    if enabled then
        local index = 0
        while index < limit do
            total = total + index
            index = index + 1
        end
    end
    return total
end
return bounded
LUA
"$BYTEVEIL_LUAC51" -o "$TMP/if-loop.luac" "$TMP/if-loop.lua"
"$BIN" --bytecode "$TMP/if-loop.luac" --format lua > "$TMP/if-loop.reconstructed.lua"
if grep -q 'PC dispatcher' "$TMP/if-loop.reconstructed.lua"; then
    echo "a loop latch at the end of an if body triggered the PC dispatcher" >&2
    exit 1
fi
grep -q '^    if r0 then$' "$TMP/if-loop.reconstructed.lua"
grep -q '^        while true do$' "$TMP/if-loop.reconstructed.lua"
"$BYTEVEIL_LUAC51" -p "$TMP/if-loop.reconstructed.lua"
IF_LOOP_ORIGINAL_PATH="$TMP/if-loop.lua"
IF_LOOP_RECONSTRUCTED_PATH="$TMP/if-loop.reconstructed.lua"
if command -v cygpath >/dev/null 2>&1; then
    IF_LOOP_ORIGINAL_PATH="$(cygpath -m "$IF_LOOP_ORIGINAL_PATH")"
    IF_LOOP_RECONSTRUCTED_PATH="$(cygpath -m "$IF_LOOP_RECONSTRUCTED_PATH")"
fi
"$BYTEVEIL_LUA51" -e "local original=assert(loadfile('$IF_LOOP_ORIGINAL_PATH'))(); local reconstructed=assert(loadfile('$IF_LOOP_RECONSTRUCTED_PATH'))(); for _, enabled in ipairs({false, true}) do for _, limit in ipairs({-1, 0, 1, 4}) do assert(original(enabled, limit) == reconstructed(enabled, limit), 'if-wrapped loop differs') end end"
cat >"$TMP/numeric-closures.lua" <<'LUA'
local functions = {}
for index = 1, 3 do
    functions[index] = function(amount)
        index = index + amount
        return index
    end
end
return functions
LUA
cat >"$TMP/generic-closures.lua" <<'LUA'
local functions = {}
for key, value in ipairs({ "alpha", "beta", "gamma" }) do
    functions[key] = function(increment, suffix)
        key = key + increment
        value = value .. suffix
        return key, value
    end
end
return functions
LUA
"$BYTEVEIL_LUAC51" -o "$TMP/numeric-closures.luac" "$TMP/numeric-closures.lua"
"$BYTEVEIL_LUAC51" -o "$TMP/generic-closures.luac" "$TMP/generic-closures.lua"
"$BIN" --bytecode "$TMP/numeric-closures.luac" --format lua > "$TMP/numeric-closures.reconstructed.lua"
"$BIN" --bytecode "$TMP/generic-closures.luac" --format lua > "$TMP/generic-closures.reconstructed.lua"
if grep -q 'PC dispatcher' "$TMP/numeric-closures.reconstructed.lua"; then
    echo "numeric for with a captured loop variable was not reconstructed structurally" >&2
    exit 1
fi
if grep -q 'PC dispatcher' "$TMP/generic-closures.reconstructed.lua"; then
    echo "generic for with captured loop variables was not reconstructed structurally" >&2
    exit 1
fi
NUMERIC_ORIGINAL_PATH="$TMP/numeric-closures.lua"
NUMERIC_RECONSTRUCTED_PATH="$TMP/numeric-closures.reconstructed.lua"
GENERIC_ORIGINAL_PATH="$TMP/generic-closures.lua"
GENERIC_RECONSTRUCTED_PATH="$TMP/generic-closures.reconstructed.lua"
if command -v cygpath >/dev/null 2>&1; then
    NUMERIC_ORIGINAL_PATH="$(cygpath -m "$NUMERIC_ORIGINAL_PATH")"
    NUMERIC_RECONSTRUCTED_PATH="$(cygpath -m "$NUMERIC_RECONSTRUCTED_PATH")"
    GENERIC_ORIGINAL_PATH="$(cygpath -m "$GENERIC_ORIGINAL_PATH")"
    GENERIC_RECONSTRUCTED_PATH="$(cygpath -m "$GENERIC_RECONSTRUCTED_PATH")"
fi
"$BYTEVEIL_LUA51" "$ROOT/tests/lua51_loop_closure_runtime.lua" "$NUMERIC_ORIGINAL_PATH" "$NUMERIC_RECONSTRUCTED_PATH" "$GENERIC_ORIGINAL_PATH" "$GENERIC_RECONSTRUCTED_PATH"
cat >"$TMP/condition-chains.lua" <<'LUA'
local function classify(value)
    if value == "alpha" or value == "beta" or value == "gamma" then
        return "matched"
    end
    return "other"
end
local function choose(first, second, third)
    if first or second or third then return "truthy" end
    return "falsy"
end
local function compareWithCalls(a, b, c, d, probe)
    return a == b and probe("first") == c and probe("second") == d
end
local function callChainElse(enabled, probe, record)
    if enabled and probe("first") and probe("second") then
        record("body")
    else
        record("fallback")
    end
end
local function mixedValue(getValue, mapValue, fallback)
    local value = getValue()
    return mapValue(value) and value or fallback
end
local function captureBoundary(tag, maybeGetter)
    if tag == "go" then
        tag = maybeGetter() or tag
    end
    return function() return tag end
end
local function incrementOrFallback(value, fallback)
    return value and (value + 1) or fallback
end
local function nestedSharedJoin(enabled, probe, action, fallback)
    if enabled then
        local value = probe()
        if not value then action(value) end
    else
        fallback()
    end
end
return classify, choose, compareWithCalls, callChainElse, mixedValue, captureBoundary, incrementOrFallback, nestedSharedJoin
LUA
"$BYTEVEIL_LUAC51" -o "$TMP/condition-chains.luac" "$TMP/condition-chains.lua"
"$BIN" --bytecode "$TMP/condition-chains.luac" --format lua > "$TMP/condition-chains.reconstructed.lua"
if grep -q 'PC dispatcher' "$TMP/condition-chains.reconstructed.lua"; then
    echo "a short-circuit condition chain was not reconstructed structurally" >&2
    exit 1
fi
if ! grep -q '__byteveil_condition_' "$TMP/condition-chains.reconstructed.lua"; then
    echo "a call-separated boolean condition chain lost its lazy result structure" >&2
    exit 1
fi
"$BYTEVEIL_LUAC51" -p "$TMP/condition-chains.reconstructed.lua"
CONDITION_ORIGINAL_PATH="$TMP/condition-chains.lua"
CONDITION_RECONSTRUCTED_PATH="$TMP/condition-chains.reconstructed.lua"
if command -v cygpath >/dev/null 2>&1; then
    CONDITION_ORIGINAL_PATH="$(cygpath -m "$CONDITION_ORIGINAL_PATH")"
    CONDITION_RECONSTRUCTED_PATH="$(cygpath -m "$CONDITION_RECONSTRUCTED_PATH")"
fi
"$BYTEVEIL_LUA51" "$ROOT/tests/lua51_condition_chain_runtime.lua" "$CONDITION_ORIGINAL_PATH" "$CONDITION_RECONSTRUCTED_PATH"
cat >"$TMP/loop-breaks.lua" <<'LUA'
local function tally(n)
    local total = 0
    for i = 1, n do
        if i == 3 then break end
        total = total + i
    end
    local keys = 0
    for key, value in ipairs({"a", "b", "c"}) do
        if key == 2 then break end
        keys = keys + key
    end
    local countdown = 0
    while n > 0 do
        if n == 1 then break end
        countdown = countdown + 1
        n = n - 1
    end
    local repeated = 0
    repeat
        repeated = repeated + 1
        if repeated == 2 then break end
    until repeated > 10
    return total, keys, countdown, n, repeated
end
return tally
LUA
"$BYTEVEIL_LUAC51" -o "$TMP/loop-breaks.luac" "$TMP/loop-breaks.lua"
"$BIN" --bytecode "$TMP/loop-breaks.luac" --format lua > "$TMP/loop-breaks.reconstructed.lua"
if grep -q 'PC dispatcher' "$TMP/loop-breaks.reconstructed.lua"; then
    echo "structured loops containing break still use a PC dispatcher" >&2
    exit 1
fi
"$BYTEVEIL_LUAC51" -p "$TMP/loop-breaks.reconstructed.lua"
LOOP_BREAKS_ORIGINAL_PATH="$TMP/loop-breaks.lua"
LOOP_BREAKS_RECONSTRUCTED_PATH="$TMP/loop-breaks.reconstructed.lua"
if command -v cygpath >/dev/null 2>&1; then
    LOOP_BREAKS_ORIGINAL_PATH="$(cygpath -m "$LOOP_BREAKS_ORIGINAL_PATH")"
    LOOP_BREAKS_RECONSTRUCTED_PATH="$(cygpath -m "$LOOP_BREAKS_RECONSTRUCTED_PATH")"
fi
"$BYTEVEIL_LUA51" "$ROOT/tests/lua51_loop_break_runtime.lua" "$LOOP_BREAKS_ORIGINAL_PATH" "$LOOP_BREAKS_RECONSTRUCTED_PATH"
cat >"$TMP/loop-early-return.lua" <<'LUA'
local function findValue(values)
    for _, value in ipairs(values) do
        if value == "stop" then return value end
    end
    return "missing"
end
return findValue
LUA
"$BYTEVEIL_LUAC51" -o "$TMP/loop-early-return.luac" "$TMP/loop-early-return.lua"
"$BIN" --bytecode "$TMP/loop-early-return.luac" --format lua > "$TMP/loop-early-return.reconstructed.lua"
"$BYTEVEIL_LUAC51" -p "$TMP/loop-early-return.reconstructed.lua"
LOOP_RETURN_ORIGINAL_PATH="$TMP/loop-early-return.lua"
LOOP_RETURN_RECONSTRUCTED_PATH="$TMP/loop-early-return.reconstructed.lua"
if command -v cygpath >/dev/null 2>&1; then
    LOOP_RETURN_ORIGINAL_PATH="$(cygpath -m "$LOOP_RETURN_ORIGINAL_PATH")"
    LOOP_RETURN_RECONSTRUCTED_PATH="$(cygpath -m "$LOOP_RETURN_RECONSTRUCTED_PATH")"
fi
"$BYTEVEIL_LUA51" "$ROOT/tests/lua51_loop_return_runtime.lua" "$LOOP_RETURN_ORIGINAL_PATH" "$LOOP_RETURN_RECONSTRUCTED_PATH"
cat >"$TMP/and-chain.lua" <<'LUA'
local function check(a, b, c)
    if a and b and c then return "yes" end
    return "no"
end
local function bounded(n)
    local i, total = 0, 0
    while i < n and i < 3 do
        i = i + 1
        total = total + i
    end
    return total
end
local function count(flag, n)
    if flag then
        repeat
            n = n - 1
        until n <= 0
    end
    return n
end
local function choose(probe)
    local result
    if probe(1) or probe(2) or probe(3) or probe(4) then
        result = "yes"
    else
        result = "no"
    end
    return result
end
local function chooseMixed(probe)
    if probe(1) or (probe(2) and probe(3)) then
        return "yes"
    end
    return "no"
end
return check, bounded, count, choose, chooseMixed
LUA
"$BYTEVEIL_LUAC51" -o "$TMP/and-chain.luac" "$TMP/and-chain.lua"
"$BIN" --bytecode "$TMP/and-chain.luac" --format lua > "$TMP/and-chain.reconstructed.lua"
if grep -q 'PC dispatcher' "$TMP/and-chain.reconstructed.lua"; then
    echo "a shared-exit and-chain still uses a PC dispatcher" >&2
    exit 1
fi
"$BYTEVEIL_LUAC51" -p "$TMP/and-chain.reconstructed.lua"
AND_CHAIN_ORIGINAL_PATH="$TMP/and-chain.lua"
AND_CHAIN_RECONSTRUCTED_PATH="$TMP/and-chain.reconstructed.lua"
if command -v cygpath >/dev/null 2>&1; then
    AND_CHAIN_ORIGINAL_PATH="$(cygpath -m "$AND_CHAIN_ORIGINAL_PATH")"
    AND_CHAIN_RECONSTRUCTED_PATH="$(cygpath -m "$AND_CHAIN_RECONSTRUCTED_PATH")"
fi
"$BYTEVEIL_LUA51" "$ROOT/tests/lua51_and_chain_runtime.lua" "$AND_CHAIN_ORIGINAL_PATH" "$AND_CHAIN_RECONSTRUCTED_PATH"
printf 'Lua 5.1 reconstruction tests: PASS\n'
