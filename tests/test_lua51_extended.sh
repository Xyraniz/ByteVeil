#!/usr/bin/env bash
set -euo pipefail
BIN="${1:-./build/byteveil}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cat >"$TMP/flow.lua" <<'LUA'
local function classify(value)
    if value < 0 then return "negative" end
    local result = "positive"
    for i = 1, 2 do result = result .. i end
    return result
end
return classify(...)
LUA
luac5.1 -o "$TMP/flow.luac" "$TMP/flow.lua"
"$BIN" --bytecode "$TMP/flow.luac" --format lua >"$TMP/lifted.lua"
"$BIN" --bytecode "$TMP/flow.luac" --format structured >"$TMP/structured.lua"
lua5.1 -e "assert(loadfile('$TMP/lifted.lua')); assert(loadfile('$TMP/structured.lua'))"
grep -q 'LOADBOOL\|FORPREP\|FORLOOP\|JMP' "$TMP/structured.lua"
"$BIN" --bytecode "$ROOT/tests/fixtures/lua51-sample.luac" --format json >"$TMP/sample.json"
python3 - "$TMP/sample.json" <<'PY'
import json, sys
x = json.load(open(sys.argv[1]))
assert x["root_function"]["analysis"]["alias_hazards"] >= 0
assert x["root_function"]["children"]
PY
printf 'Lua 5.1 extended tests: PASS\n'
