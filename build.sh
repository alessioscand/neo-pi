#!/bin/bash
# neo-pi build: ./build.sh [4|3|all]
# Multicore (default): Circle in circle-rpi4mc / circle-rpi3mc, built with
# ARM_ALLOW_MULTI_CORE; the video chip lines are drawn on core 1
# (patches/geolith-mc.py). NEOPI_SINGLE=1 ./build.sh builds the single core
# kernels of 0.6.3 and earlier (Circle in circle-rpi4 / circle-rpi3).
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
GEO="$ROOT/third_party/geolith"
TARGETS="${1:-all}"
[ "$TARGETS" = "all" ] && TARGETS="4 3"

for P in $TARGETS; do
	B="$ROOT/build/rpi$P"
	rm -rf "$B"; mkdir -p "$B"
	# flatten all sources into the build directory (one object dir per target)
	cp "$ROOT"/src/*.c "$ROOT"/src/*.cpp "$B"/
	for f in src/m68k/m68kcpu.c src/m68k/m68kops.c src/z80/z80.c \
		 src/ymfm/ymfm_adpcm.c src/ymfm/ymfm_opn.c src/ymfm/ymfm_ssg.c \
		 src/geo.c src/geo_cd.c src/geo_lc8951.c src/geo_lspc.c src/geo_m68k.c \
		 src/geo_memcard.c src/geo_mixer.c src/geo_neo.c src/geo_rtc.c \
		 src/geo_serial.c src/geo_vfs.c src/geo_ymfm.c src/geo_z80.c \
		 deps/miniz/miniz.c deps/speex/resample.c; do
		cp "$GEO/$f" "$B"/
	done
	# setjmp/longjmp from newlib
	(cd "$B" && "$ROOT"/toolchain/xpack-aarch64-none-elf-gcc-14.2.1-1.1/bin/aarch64-none-elf-ar x \
		"$ROOT"/toolchain/xpack-aarch64-none-elf-gcc-14.2.1-1.1/aarch64-none-elf/lib/libc.a libc_a-setjmp.o)
	CIRCLE="$ROOT/circle-rpi${P}mc"
	[ -n "$NEOPI_SINGLE" ] && CIRCLE="$ROOT/circle-rpi$P"
	sed "s|@CIRCLEHOME@|$CIRCLE|; s|@GEO@|$GEO|; s|@SRC@|$ROOT/src|" \
		"$ROOT/src/Makefile.in" > "$B/Makefile"
	if [ -z "$NEOPI_SINGLE" ]; then
		python3 "$ROOT/patches/geolith-mc.py" "$B/geo_lspc.c"
		echo "DEFINE += -DNEOPI_MC" >> "$B/Makefile"
	fi
	make -C "$B" -j16
done

# sanity check: the whole kernel (code + data + bss) must fit in KERNEL_MAX_SIZE
for P in $TARGETS; do
	ELF=$(ls "$ROOT"/build/rpi$P/*.elf)
	END=$("$ROOT"/toolchain/xpack-aarch64-none-elf-gcc-14.2.1-1.1/bin/aarch64-none-elf-nm "$ELF" | awk '$3=="__bss_end"||$3=="_end"{print $1}' | sort | tail -1)
	LIMIT=$((0x80000 + 0x1000000))
	if [ $((16#$END)) -ge $LIMIT ]; then echo "ERROR: kernel rpi$P too large (ends at 0x$END)"; exit 1; fi
	echo "rpi$P: kernel ends at 0x$END (limit $(printf 0x%x $LIMIT)) OK"
done
