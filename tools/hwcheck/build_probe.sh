#!/bin/sh
# Builds the address probes: tools/hwcheck/build_probe.sh /path/to/rust-xbox360
set -e
cd "$(dirname "$0")"
FORGE=${1:?usage: build_probe.sh /path/to/rust-xbox360}
SDK="$FORGE/src/sdk"
XEX_LINK=${XEX_LINK:-"$FORGE/target/release/xex-link"}
OUT=../../build/probe
mkdir -p "$OUT/kit/Probe" "$OUT/kit/SideLoading"
T=--target=powerpc-unknown-none-eabi
CF="$T -O2 -ffreestanding -fno-builtin -I$SDK/include -Wall"
clang $T -I"$SDK/thunks" -c "$SDK/thunks/ppc32.S" -o "$OUT/thunks.o"
clang $T -I"$SDK/crt" -c "$SDK/crt/dll.S" -o "$OUT/crt_dll.o"
clang $CF -c "$SDK/crt/main.c" -o "$OUT/crt_main.o"
clang $T -I"$SDK/crt" -c "$SDK/crt/title.S" -o "$OUT/crt_title.o"

link() { # manifest objects... output
	m=$1; shift; out=$1; shift
	"$XEX_LINK" link-script "$OUT/tmp.x" --manifest "$m"
	ld.lld "$@" --gc-sections -z norelro -T"$OUT/tmp.x" --no-rosegment -o "$OUT/tmp.elf"
	"$XEX_LINK" "$OUT/tmp.elf" "$out" --manifest "$m" >/dev/null
}

# Probe title
printf 'title_id = 0x54505052\nload_address = 0x82000000\nversion = "1.0.0.0"\n' > "$OUT/title.toml"
clang $CF -c probe_title.c -o "$OUT/probe_title.o"
link "$OUT/title.toml" "$OUT/kit/Probe/Probe.xex" "$OUT/probe_title.o" "$OUT/crt_main.o" "$OUT/crt_title.o" "$OUT/thunks.o"

# Do-nothing DLL at each test address
clang $CF -c probe_dll.c -o "$OUT/probe_dll.o"
for b in 8400 8600 8800 8A00 8C00 8E00 8F00 8FF0; do
	printf 'load_address = 0x%s0000\nmodule_flags = ["TITLE", "DLL"]\nversion = "1.0.0.0"\ntls_slots = 0\n' $b > "$OUT/dll.toml"
	link "$OUT/dll.toml" "$OUT/kit/Probe/Dll$b.xex" "$OUT/probe_dll.o" "$OUT/crt_dll.o" "$OUT/thunks.o"
done

# LEGO Dimensions probe, sideloaded at 0x8F000000
printf 'load_address = 0x8f000000\nmodule_flags = ["TITLE", "DLL"]\nversion = "1.0.0.0"\ntls_slots = 0\n' > "$OUT/dll.toml"
clang $CF -c lego_probe.c -o "$OUT/lego_probe.o"
link "$OUT/dll.toml" "$OUT/kit/SideLoading/LegoProbe.xex" "$OUT/lego_probe.o" "$OUT/crt_dll.o" "$OUT/thunks.o"
ls -l "$OUT/kit/Probe" "$OUT/kit/SideLoading"

# LEGO Dimensions loader probe (loads ToyPortal360 copies from inside the game), at 0x8F000000
clang $CF -c lego_loader.c -o "$OUT/lego_loader.o"
link "$OUT/dll.toml" "$OUT/kit/SideLoading/LegoLoader.xex" "$OUT/lego_loader.o" "$OUT/crt_dll.o" "$OUT/thunks.o"

# Probe2: loads the ToyPortal360 copies and LegoLoader from a program
printf 'title_id = 0x54505033\nload_address = 0x82000000\nversion = "1.0.0.0"\n' > "$OUT/title2.toml"
clang $CF -c probe2_title.c -o "$OUT/probe2_title.o"
link "$OUT/title2.toml" "$OUT/kit/Probe/Probe2.xex" "$OUT/probe2_title.o" "$OUT/crt_main.o" "$OUT/crt_title.o" "$OUT/thunks.o"
