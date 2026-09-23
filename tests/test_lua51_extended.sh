#!/usr/bin/env bash
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"
byteveil_find_python
if ! byteveil_find_lua51; then
    printf 'Lua 5.1 extended tests: SKIP (Lua 5.1 interpreter/compiler not installed)\n'
    exit 0
fi
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
"$BYTEVEIL_LUAC51" -o "$TMP/flow.luac" "$TMP/flow.lua"
"$BIN" --bytecode "$TMP/flow.luac" --format lua >"$TMP/lifted.lua"
"$BIN" --bytecode "$TMP/flow.luac" --format structured >"$TMP/structured.lua"
LIFTED_PATH="$TMP/lifted.lua"
STRUCTURED_PATH="$TMP/structured.lua"
if command -v cygpath >/dev/null 2>&1; then
    LIFTED_PATH="$(cygpath -m "$LIFTED_PATH")"
    STRUCTURED_PATH="$(cygpath -m "$STRUCTURED_PATH")"
fi
"$BYTEVEIL_LUA51" -e "assert(loadfile('$LIFTED_PATH')); assert(loadfile('$STRUCTURED_PATH'))"
grep -q 'LOADBOOL\|FORPREP\|FORLOOP\|JMP' "$TMP/structured.lua"
"$BIN" --bytecode "$ROOT/tests/fixtures/lua51-sample.luac" --format json >"$TMP/sample.json"
"$BYTEVEIL_PYTHON" - "$TMP/sample.json" <<'PY'
import json, sys
x = json.load(open(sys.argv[1]))
assert x["root_function"]["analysis"]["alias_hazards"] >= 0
assert x["root_function"]["children"]
PY
printf 'Lua 5.1 extended tests: PASS\n'
