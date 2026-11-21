#!/bin/sh
# Builds the portable core twice - little-endian x86 and big-endian 32-bit
# PowerPC (same byte order as the Xbox 360) - runs the tests on both, and
# checks the crypto against a Python translation of RPCS3's code.
set -e
cd "$(dirname "$0")/.."
SRC="src/core/*.cpp tests/test_core.cpp"
FLAGS="-std=c++03 -Wall -Wextra -pedantic -Wno-long-long -O2"
mkdir -p build

echo "== x86_64 (little-endian)"
g++ $FLAGS $SRC -o build/test_x86
./build/test_x86
./build/test_x86 --vectors | python3 tests/rpcs3_reference.py
echo "== tools/make_pictures.py (the player's own grid pictures)"
python3 tests/test_make_pictures.py >build/make_pictures.log || { cat build/make_pictures.log; exit 1; }
tail -n 1 build/make_pictures.log

if command -v powerpc-linux-gnu-g++-11 >/dev/null && command -v qemu-ppc >/dev/null; then
	echo "== powerpc 32-bit (big-endian, like the Xbox 360)"
	powerpc-linux-gnu-g++-11 $FLAGS -static $SRC -o build/test_ppc
	qemu-ppc build/test_ppc
	qemu-ppc build/test_ppc --vectors | python3 tests/rpcs3_reference.py
else
	echo "(skipping big-endian run: powerpc-linux-gnu-g++-11 / qemu-ppc not installed)"
fi

if command -v powerpc-linux-gnu-g++-11 >/dev/null && command -v qemu-ppc >/dev/null; then
	echo "== Xbox layer end-to-end (powerpc, fake Xbox runtime)"
	powerpc-linux-gnu-g++-11 -std=gnu++98 -O1 -Wall -Wno-unknown-pragmas -static -pthread -Itests/xdk_stub \
		src/core/*.cpp src/xbox/*.cpp tests/xdk_stub/xdk_runtime.cpp tests/test_xbox.cpp -o build/test_xbox_ppc
	qemu-ppc build/test_xbox_ppc
fi

if command -v powerpc-linux-gnu-g++-11 >/dev/null && command -v qemu-ppc >/dev/null; then
	echo "== Peer Pressure sideload build (powerpc, fake Xbox runtime)"
	powerpc-linux-gnu-g++-11 -std=gnu++98 -O1 -Wall -Wno-unknown-pragmas -static -pthread -Itests/xdk_stub -DTOYPORTAL_SIDELOAD \
		src/core/*.cpp src/xbox/*.cpp tests/xdk_stub/xdk_runtime.cpp tests/test_sideload.cpp -o build/test_sideload_ppc
	qemu-ppc build/test_sideload_ppc
fi

# Compile and link the console image with the open-source Xenon Forge
# toolchain when its checkout is given (XENON_FORGE=/path/to/rust-xbox360).
if [ -n "$XENON_FORGE" ] && command -v ld.lld >/dev/null; then
	echo "== Xenon Forge console builds (compile + link)"
	CHECK_ONLY=1 sh tools/build_forge.sh "$XENON_FORGE"
	CHECK_ONLY=1 VARIANT=sideload sh tools/build_forge.sh "$XENON_FORGE"
fi


# Run the built .xex files themselves (after tools/build_forge.sh) under
# qemu-ppc with a mini XEX loader and fake kernel/XAM.
if command -v powerpc-linux-gnu-gcc-11 >/dev/null && command -v qemu-ppc >/dev/null && \
   [ -f build/forge/ToyPortal360.xex ] && [ -f build/forge-sideload/ToyPortal360.xex ]; then
	echo "== built .xex files under emulation"
	powerpc-linux-gnu-gcc-11 -std=gnu11 -O1 -Wall -static -pthread tests/xex_run/run_xex.c -o build/run_xex
	d=$(mktemp -d); qemu-ppc build/run_xex build/forge/ToyPortal360.xex dashlaunch "$d"
	python3 tests/xex_run/check_infinity_file.py "$d/TPHDD/ToyPortal/Infinity/Sulley.bin" 0F4242
	d=$(mktemp -d); qemu-ppc build/run_xex build/forge-sideload/ToyPortal360.xex sideload "$d"
	echo "== the Peer Pressure .xex with a game that looks the toy-pad functions up (LEGO Dimensions)"
	d=$(mktemp -d); qemu-ppc build/run_xex build/forge-sideload/ToyPortal360.xex lookup "$d"
	echo "== the Peer Pressure .xex with a game that loads its portal code in a later module (Skylanders)"
	d=$(mktemp -d); qemu-ppc build/run_xex build/forge-sideload/ToyPortal360.xex modules "$d"
	echo "== the Peer Pressure .xex drawing the character grid over a game's frames (fake VdSwap)"
	d=$(mktemp -d); qemu-ppc build/run_xex build/forge-sideload/ToyPortal360.xex grid "$d"
fi
