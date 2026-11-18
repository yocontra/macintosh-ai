#!/bin/sh
#
# Host-side unit tests for macintosh-ai (plan testing tier T0).
#
# Builds the Toolbox-free SSE decoder src/chatbot/api_parse.cpp natively with
# AddressSanitizer + UndefinedBehaviorSanitizer and runs tests/host/test_sse.c
# against it. No Retro68, no Toolbox, no emulator: this is the same thing CI
# runs on ubuntu-latest.
#
# Usage:  sh tests/host/run.sh
# Env:    CC (default cc), CXX (default c++)

set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd)

CC=${CC:-cc}
CXX=${CXX:-c++}

API_PARSE_C="$ROOT/src/chatbot/api_parse.cpp"
API_PARSE_H="$ROOT/src/chatbot/api_parse.h"
TEST_C="$HERE/test_sse.c"
GASON_DIR="$ROOT/libs/net68k/net"
GASON_CPP="$GASON_DIR/gason.cpp"

SAN="-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1"
CPPFLAGS="-I$ROOT/src/chatbot -I$GASON_DIR"
CXXFLAGS="$CPPFLAGS -std=c++17 -Wall -Wextra"

for f in "$API_PARSE_C" "$API_PARSE_H" "$TEST_C" "$GASON_CPP"; do
    if [ ! -f "$f" ]; then
        echo "error: missing $f" >&2
        echo "       (api_parse.{cpp,h} are produced by the ai-api slice)" >&2
        exit 2
    fi
done

command -v "$CC" >/dev/null 2>&1 || { echo "error: C compiler '$CC' not found" >&2; exit 2; }
command -v "$CXX" >/dev/null 2>&1 || { echo "error: C++ compiler '$CXX' not found" >&2; exit 2; }

TMP=$(mktemp -d "${TMPDIR:-/tmp}/macintosh-ai-host-tests.XXXXXX") || exit 2
trap 'rm -rf "$TMP"' EXIT INT TERM

echo "== building (CC=$CC CXX=$CXX)"

# test_sse.c is plain C; it is compiled as C++ here only so the C test's calls
# bind to the C++-mangled decoder without depending on extern "C" guards.
# gason.cpp is the decode implementation behind gason.hpp -- no Toolbox, so it
# builds natively on macOS and ubuntu.
if ! "$CXX" $CXXFLAGS $SAN -x c++ \
    "$TEST_C" "$API_PARSE_C" "$GASON_CPP" \
    -o "$TMP/test_sse"; then
    echo "error: building the host test failed" >&2
    exit 2
fi

echo "== running"
if ! "$TMP/test_sse"; then
    echo "FAIL: tests/host/test_sse" >&2
    exit 1
fi

echo "tests/host: ok"
