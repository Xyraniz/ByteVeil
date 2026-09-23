#!/usr/bin/env bash

byteveil_find_python() {
    if [[ -n "${BYTEVEIL_PYTHON:-}" ]] && "$BYTEVEIL_PYTHON" -c 'import sys' >/dev/null 2>&1; then
        return 0
    fi
    local candidate
    for candidate in python3 python; do
        if "$candidate" -c 'import sys' >/dev/null 2>&1; then
            BYTEVEIL_PYTHON="$candidate"
            export BYTEVEIL_PYTHON
            return 0
        fi
    done
    printf 'No usable Python 3 interpreter found (tried python3 and python)\n' >&2
    return 1
}

byteveil_find_lua51() {
    BYTEVEIL_LUA51=""
    BYTEVEIL_LUAC51=""
    local candidate candidate_path version
    for candidate in lua5.1 lua5.1.exe lua.exe lua; do
        candidate_path="$(command -v "$candidate" 2>/dev/null || true)"
        [[ -n "$candidate_path" ]] || continue
        version="$("$candidate_path" -v 2>&1 || true)"
        if [[ "$version" == *"Lua 5.1"* ]]; then
            BYTEVEIL_LUA51="$candidate_path"
            break
        fi
    done
    for candidate in luac5.1 luac5.1.exe luac.exe luac; do
        candidate_path="$(command -v "$candidate" 2>/dev/null || true)"
        [[ -n "$candidate_path" ]] || continue
        version="$("$candidate_path" -v 2>&1 || true)"
        if [[ "$version" == *"Lua 5.1"* ]]; then
            BYTEVEIL_LUAC51="$candidate_path"
            break
        fi
    done
    [[ -n "$BYTEVEIL_LUA51" && -n "$BYTEVEIL_LUAC51" ]]
}
