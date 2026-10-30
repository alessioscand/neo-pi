#!/bin/bash
# neo-pi release package: ./make_release.sh  (run ./build.sh all first)
# Checks the kernels, the firmware and that no user data is included, then
# writes dist/neo-pi-<version>-sd.zip with everything that goes on the card.
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

VERSION=$(sed -n 's/^#define NEOPI_VERSION[[:space:]]*"\(.*\)".*/\1/p' src/neopi.h)
[ -n "$VERSION" ] || { echo "ERROR: NEOPI_VERSION not found in src/neopi.h"; exit 1; }
echo "neo-pi $VERSION"

fail () { echo "ERROR: $*"; exit 1; }

# kernels: present and built from this version
K4=build/rpi4/kernel8-rpi4.img
K3=build/rpi3/kernel8.img
for K in "$K4" "$K3"; do
	[ -f "$K" ] || fail "$K missing: run ./build.sh all"
	grep -qa "$VERSION" "$K" || fail "$K does not contain version $VERSION: rebuild"
	[ "$K" -nt src/neopi.h ] || fail "$K is older than src/neopi.h: rebuild"
done

# firmware and configuration files in dist/sd
for F in bootcode.bin start.elf start4.elf fixup.dat fixup4.dat armstub8-rpi4.bin \
	 bcm2711-rpi-4-b.dtb bcm2711-rpi-400.dtb bcm2711-rpi-cm4.dtb LICENCE.broadcom \
	 config.txt cmdline.txt README.txt LICENSES.txt neopi/PUT_BIOS_AND_GAMES_HERE.txt; do
	[ -f "dist/sd/$F" ] || fail "dist/sd/$F missing"
done
grep -q "^neo-pi $VERSION " dist/sd/README.txt || fail "dist/sd/README.txt does not start with 'neo-pi $VERSION'"

# assemble in a clean staging folder: only the files that belong on the card
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
cp -r dist/sd/. "$STAGE/"
rm -f "$STAGE"/kernel*.img
cp "$K4" "$K3" "$STAGE/"
# the neopi folder ships with the placeholder only: no BIOS, games, BENCH,
# logs, saves, screenshots or settings from a test card
find "$STAGE/neopi" -mindepth 1 ! -name PUT_BIOS_AND_GAMES_HERE.txt -exec rm -rf {} +

LEFT=$(find "$STAGE" -iname '*.neo' -o -iname 'neogeo.zip' -o -iname 'BENCH*' \
	-o -iname '*.cfg' -o -iname 'LOG.TXT' -o -iname 'LAST.TXT' -o -iname 'PROFILE.TXT')
[ -z "$LEFT" ] || fail "user data in the package: $LEFT"

OUT="$ROOT/dist/neo-pi-$VERSION-sd.zip"
rm -f "$OUT"
(cd "$STAGE" && zip -qrX "$OUT" .)
echo "written $OUT"
unzip -l "$OUT" | tail -n +4 | head -n -2 | awk '{print "  " $4}'
