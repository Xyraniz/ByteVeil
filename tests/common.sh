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
