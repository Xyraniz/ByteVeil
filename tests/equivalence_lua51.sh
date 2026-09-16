#!/usr/bin/env bash
set -u
if [ "$#" -ne 2 ]; then echo "usage: $0 ORIGINAL.lua CANDIDATE.lua" >&2; exit 2; fi
ORIGINAL="$1"
CANDIDATE="$2"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
lua5.1 "$ORIGINAL" >"$TMP/original.out" 2>"$TMP/original.err"; a=$?
lua5.1 "$CANDIDATE" >"$TMP/candidate.out" 2>"$TMP/candidate.err"; b=$?
if [ "$a" -ne "$b" ]; then echo "behavior mismatch: exit $a != $b" >&2; exit 1; fi
if ! cmp -s "$TMP/original.out" "$TMP/candidate.out"; then echo "behavior mismatch: stdout differs" >&2; diff -u "$TMP/original.out" "$TMP/candidate.out" >&2 || true; exit 1; fi
if ! cmp -s "$TMP/original.err" "$TMP/candidate.err"; then echo "behavior mismatch: stderr differs" >&2; diff -u "$TMP/original.err" "$TMP/candidate.err" >&2 || true; exit 1; fi
echo "Lua 5.1 behavioral equivalence: PASS"
