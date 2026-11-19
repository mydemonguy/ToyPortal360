#!/bin/sh
# Builds ToyPortal360.xex with the open-source Xenon Forge toolchain
# (https://github.com/landaire/rust-xbox360) instead of Microsoft's SDK.
#
#   tools/build_forge.sh /path/to/rust-xbox360            DashLaunch plugin
#   VARIANT=sideload tools/build_forge.sh /path/to/...      Peer Pressure sideload DLL
#
# Needs: clang and ld.lld (LLVM 18 or newer), plus cargo to build Xenon
# Forge's xex-link packer once. Tested against Xenon Forge commit
# 2fcc78c40f410b52f0bd86b1c68257d79da645ee (2026-11-16).
#
# Set XEX_LINK=/path/to/xex-link to use an xex-link you already built.
# With CHECK_ONLY=1 the script stops after linking build/forge/ToyPortal360.elf
# (no xex-link needed), which proves the plugin compiles and links.
set -e
cd "$(dirname "$0")/.."

FORGE=${1:?usage: tools/build_forge.sh /path/to/rust-xbox360}
SDK="$FORGE/src/sdk"
VARIANT=${VARIANT:-dashlaunch}
case "$VARIANT" in
	dashlaunch) OUT=build/forge; MANIFEST=tools/forge/xex.toml; SCRIPT=tools/forge/link.x; DEFINES= ;;
	sideload) OUT=build/forge-sideload; MANIFEST=tools/forge/sideload/xex.toml; SCRIPT=tools/forge/sideload/link.x; DEFINES=-DTOYPORTAL_SIDELOAD
		[ "${BREADCRUMBS:-0}" = 1 ] && DEFINES="$DEFINES -DTOYPORTAL_BREADCRUMBS" ;;
	*) echo "VARIANT must be dashlaunch or sideload"; exit 1 ;;
esac
echo "building the $VARIANT variant"
# LOAD_ADDRESS=0x... (sideload only) builds a copy at another address.
if [ "$VARIANT" = sideload ] && [ -n "$LOAD_ADDRESS" ]; then
	mkdir -p "$OUT"
	sed "s/^load_address *=.*/load_address = $LOAD_ADDRESS/" "$MANIFEST" > "$OUT/xex.toml"
	MANIFEST="$OUT/xex.toml"
fi
# Title DLLs load at 0x82000000-0x8FFFFFFF with 64 KB pages. Measured on a
# console: 0x8C000000 and 0x8E000000 are always taken in a title's process.
if [ "$VARIANT" = sideload ]; then
	BASE=$(sed -n 's/^load_address *= *\(0x[0-9a-fA-F]*\).*/\1/p' "$MANIFEST")
	if [ $((BASE)) -lt $((0x82000000)) ] || [ $((BASE)) -ge $((0x90000000)) ]; then
		echo "error: load_address $BASE must be in 0x82000000-0x8FFFFFFF"; exit 1
	fi
	case $((BASE >> 24)) in 140|142) echo "error: load_address $BASE: 0x8C/0x8E00000 are taken on a console"; exit 1 ;; esac
fi
mkdir -p "$OUT"
rm -f "$OUT"/*.o

TARGET=--target=powerpc-unknown-none-eabi
CXXFLAGS="$TARGET -O2 -fno-builtin -fshort-wchar -fno-exceptions -fno-rtti -fno-threadsafe-statics \
 -ffunction-sections -fdata-sections -DTOYPORTAL_FORGE $DEFINES -Isrc/forge -Isrc/forge/include -Wall"

echo "== compiling"
for f in src/core/*.cpp src/xbox/*.cpp src/forge/runtime.cpp; do
	clang $CXXFLAGS -c "$f" -o "$OUT/$(basename "$f" .cpp).o"
done
# BREADCRUMBS=1 (sideload only): start-up marker files, see src/forge/breadcrumb.c.
if [ "${BREADCRUMBS:-0}" = 1 ]; then
	clang $TARGET -O2 -ffreestanding -fno-builtin -I"$SDK/include" -Wall -c src/forge/breadcrumb.c -o "$OUT/breadcrumb.o"
fi
# Our assembly, Xenon Forge's DLL entry (it calls DllMain with the console's
# registers saved) and its import stubs.
clang $TARGET -I"$SDK/thunks" -c src/forge/forge.S -o "$OUT/forge_s.o"
clang $TARGET -I"$SDK/crt" -c "$SDK/crt/dll.S" -o "$OUT/dll_s.o"
clang $TARGET -I"$SDK/thunks" -c "$SDK/thunks/ppc32.S" -o "$OUT/ppc32_s.o"

echo "== linking"
ld.lld "$OUT"/*.o --gc-sections -z norelro -T "$SCRIPT" --no-rosegment -o "$OUT/ToyPortal360.elf"
echo "linked $OUT/ToyPortal360.elf"

if [ "${CHECK_ONLY:-0}" = 1 ]; then
	exit 0
fi

if [ -z "$XEX_LINK" ]; then
	echo "== building xex-link"
	(cd "$FORGE" && cargo build --release -p xex-link -p xbox360-asm-rewrite)
	XEX_LINK="$FORGE/target/release/xex-link"
fi

# The checked-in link script must match what xex-link derives from the manifest.
"$XEX_LINK" link-script "$OUT/link.generated.x" --manifest "$MANIFEST"
if ! diff -q -I '^/\*' -I '^ \*' "$OUT/link.generated.x" "$SCRIPT" >/dev/null; then
	echo "warning: $SCRIPT differs from xex-link's script; relinking with xex-link's"
	ld.lld "$OUT"/*.o --gc-sections -z norelro -T "$OUT/link.generated.x" --no-rosegment -o "$OUT/ToyPortal360.elf"
fi

# Xenon Forge's check for atomics the console loader rejects (optional).
ASM_REWRITE=${ASM_REWRITE:-"$FORGE/target/release/xbox360-asm-rewrite"}
if [ -x "$ASM_REWRITE" ]; then
	echo "== reservations check"
	"$ASM_REWRITE" reservations "$OUT/ToyPortal360.elf"
fi

echo "== packing"
"$XEX_LINK" "$OUT/ToyPortal360.elf" "$OUT/ToyPortal360.xex" --manifest "$MANIFEST"
ls -l "$OUT/ToyPortal360.xex"
