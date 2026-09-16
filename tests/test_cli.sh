#!/usr/bin/env bash
set -u
BIN="${1:-./build/byteveil}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cat >"$TMP/sample.luau" <<'LUA'
local x = 1 + 2
return x
LUA
"$BIN" --version | grep -q '^ByteVeil '
"$BIN" --format json "$TMP/sample.luau" >"$TMP/a.json"
"$BIN" --format json "$TMP/sample.luau" >"$TMP/b.json"
grep -q '"root_function"' "$TMP/a.json"
grep -q '"opcode_name"' "$TMP/a.json"
cmp "$TMP/a.json" "$TMP/b.json"
"$BIN" --disassemble "$TMP/sample.luau" >"$TMP/disassembly"
grep -q '^function 0' "$TMP/disassembly"
grep -q 'LOAD' "$TMP/disassembly"
"$BIN" --cfg "$TMP/graph.dot" "$TMP/sample.luau" >/dev/null
grep -q '^digraph byteveil_cfg' "$TMP/graph.dot"
printf 'MoonSec V3\n' >"$TMP/protected.lua"
"$BIN" --analyze "$TMP/protected.lua" | grep -q 'moonsec_marker: yes'
printf 'error("MUST_NOT_EXECUTE")\n' >"$TMP/noexec.luau"
"$BIN" --analyze "$TMP/noexec.luau" >"$TMP/static-report" 2>"$TMP/static-error"
grep -q '^format:' "$TMP/static-report"
! grep -q 'MUST_NOT_EXECUTE' "$TMP/static-error"
printf '\033Luau' >"$TMP/truncated.luau"
if "$BIN" --bytecode "$TMP/truncated.luau" >/dev/null 2>&1; then exit 1; fi
echo "synthetic CLI tests: PASS"
