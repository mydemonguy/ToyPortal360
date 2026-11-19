#!/bin/sh
# Builds the two hardware checks: tools/hwcheck/build.sh /path/to/rust-xbox360
set -e
cd "$(dirname "$0")"
FORGE=${1:?usage: build.sh /path/to/rust-xbox360}
SDK="$FORGE/src/sdk"
XEX_LINK=${XEX_LINK:-"$FORGE/target/release/xex-link"}
OUT=../../build/hwcheck
mkdir -p "$OUT"
T=--target=powerpc-unknown-none-eabi
CF="$T -O2 -ffreestanding -fno-builtin -I$SDK/include -Wall"
clang $T -I"$SDK/thunks" -c "$SDK/thunks/ppc32.S" -o "$OUT/thunks.o"
# Standalone program
clang $CF -c title.c -o "$OUT/title.o"
clang $CF -c "$SDK/crt/main.c" -o "$OUT/crt_main.o"
clang $T -I"$SDK/crt" -c "$SDK/crt/title.S" -o "$OUT/crt_title.o"
"$XEX_LINK" link-script "$OUT/title.x" --manifest title.toml
ld.lld "$OUT/title.o" "$OUT/crt_main.o" "$OUT/crt_title.o" "$OUT/thunks.o" --gc-sections -z norelro -T"$OUT/title.x" --no-rosegment -o "$OUT/ForgeCheck.elf"
"$XEX_LINK" "$OUT/ForgeCheck.elf" "$OUT/ForgeCheck.xex" --manifest title.toml
# Title DLL
clang $CF -c dll.c -o "$OUT/dll.o"
clang $T -I"$SDK/crt" -c "$SDK/crt/dll.S" -o "$OUT/crt_dll.o"
"$XEX_LINK" link-script "$OUT/dll.x" --manifest dll.toml
ld.lld "$OUT/dll.o" "$OUT/crt_dll.o" "$OUT/thunks.o" --gc-sections -z norelro -T"$OUT/dll.x" --no-rosegment -o "$OUT/DllCheck.elf"
"$XEX_LINK" "$OUT/DllCheck.elf" "$OUT/DllCheck.xex" --manifest dll.toml
