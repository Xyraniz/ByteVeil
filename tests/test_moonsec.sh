#!/usr/bin/env bash
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"
byteveil_find_python

BIN="${1:-./build/byteveil}"
FIXTURE="tests/fixtures/moonsec-v3-serialized.lua"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

"$BIN" --format moonsec "$FIXTURE" >"$TMP/moonsec.json"
"$BIN" --format unpack "$FIXTURE" >"$TMP/unpack.json"
"$BIN" --format moonsec-ir "$FIXTURE" >"$TMP/moonsec-ir.json"
"$BIN" --format moonsec-bytecode "$FIXTURE" -o "$TMP/serialized.bin" -q
"$BYTEVEIL_PYTHON" - "$TMP/moonsec.json" "$TMP/unpack.json" "$TMP/moonsec-ir.json" <<'PY'
import json
import sys

moonsec = json.load(open(sys.argv[1], encoding='utf-8'))
unpack = json.load(open(sys.argv[2], encoding='utf-8'))
ir = json.load(open(sys.argv[3], encoding='utf-8'))

for report in (moonsec, unpack):
    assert report['executed'] is False
    assert report['recognized'] is True
    assert report['status'] == 'serialized-bytecode-extracted'
    payload = report['payload']
    assert payload['kind'] == 'moonsec-v3-serialized-bytecode'
    assert payload['bytes'] == 40
    assert payload['decoder_key'] == 7
    assert payload['prototype_layout'] == 'constants,instructions,functions,parameters'
    assert payload['constant_tags'] == '0:boolean,1:nil,2:number,3:string'
    assert payload['functions'] == 1
    assert payload['instructions'] == 1
    assert payload['constants'] == 3
    assert payload['checksum'].startswith('fnv1a64:')

assert ir['executed'] is False
assert ir['recognized'] is True
assert ir['status'] == 'virtual-ir-extracted'
assert ir['virtual_opcode_mapping'] == 'unresolved'
root = ir['root_function']
assert root['parameters'] == 0
assert root['constants'] == [
    {'type': 'boolean', 'value': True},
    {'type': 'number', 'bytes_hex': '000000000000f03f'},
    {'type': 'string', 'bytes_hex': '6f6b'},
]
assert root['instructions'] == [{
    'pc': 0, 'descriptor': 0, 'op_num': 30, 'a': 0, 'b': 1, 'c': 0,
    'is_k_a': False, 'is_k_b': False, 'is_k_c': False,
}]
assert root['children'] == []
PY
"$BYTEVEIL_PYTHON" - "$TMP/serialized.bin" <<'PY'
import sys

payload = open(sys.argv[1], 'rb').read()
assert len(payload) == 40
assert payload[:4] == b'\x03\0\0\0'
assert payload[-5:] == b'\0\0\0\0\0'
PY

printf 'MoonSec V3\nlocal bad = "0123456789abcdefaa"\n' >"$TMP/malformed.lua"
"$BIN" --format moonsec "$TMP/malformed.lua" >"$TMP/malformed.json"
"$BYTEVEIL_PYTHON" - "$TMP/malformed.json" <<'PY'
import json
import sys

report = json.load(open(sys.argv[1], encoding='utf-8'))
assert report['executed'] is False
assert report['recognized'] is True
assert report['status'] == 'serialized-bytecode-not-recovered'
assert 'payload' not in report
assert report['reason']
PY

echo "MoonSec V3 static extraction tests: PASS"
