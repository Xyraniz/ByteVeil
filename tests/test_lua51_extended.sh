#!/usr/bin/env bash
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"
byteveil_find_python
BIN="${1:-./build/byteveil}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if ! command -v luac5.1 >/dev/null 2>&1 || ! command -v lua5.1 >/dev/null 2>&1; then
    printf 'Lua 5.1 extended tests: SKIP (lua5.1/luac5.1 not installed)\n'
    exit 0
fi
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
"$BYTEVEIL_PYTHON" - "$TMP/sample.json" <<'PY'
import json, sys
x = json.load(open(sys.argv[1]))
assert x["root_function"]["analysis"]["alias_hazards"] >= 0
assert x["root_function"]["children"]
PY
printf 'Lua 5.1 extended tests: PASS\n'
