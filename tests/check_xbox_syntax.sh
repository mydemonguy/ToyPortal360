#!/bin/sh
# Syntax/type-checks the Xbox-only sources with GCC against tests/xdk_stub.
# This cannot replace a real build with the Xbox 360 SDK; it only catches
# typos and type errors without the SDK.
set -e
cd "$(dirname "$0")/.."
for f in src/xbox/*.cpp; do
	powerpc-linux-gnu-g++-11 -std=gnu++98 -fsyntax-only -Wall -Wno-unknown-pragmas -Itests/xdk_stub "$f"
	echo "ok  $f"
done
