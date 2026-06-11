#!/bin/sh

CORES="$(nproc --all)"

SRC="Source/Atmosphere/stratosphere/loader/"
DEST="build/stratosphere/loader/"
mkdir -p "dist/atmosphere/kips/"
mkdir -p "$DEST"

echo
echo "*** Patching loader ***"
cp -vr "$SRC"/. "$DEST"/
echo

echo "CORES: $CORES"
echo

echo "*** Compiling loader ***"
cd build/stratosphere/loader || exit 1
make -j$CORES
hactool -t kip1 out/nintendo_nx_arm64_armv8a/release/loader.kip --uncompress=hoc.kip
cd ../../../ # exit
cp -v build/stratosphere/loader/hoc.kip dist/atmosphere/kips/hoc.kip

cd Source/hoc-clk/
./build.sh
cp -r dist/ ../../

cd ../../

echo "*** Compiling horizon-oc-monitor ***"
cd Source/Horizon-OC-Monitor/
make -j$CORES
cp -v Horizon-OC-Monitor.ovl ../../dist/switch/.overlays/Horizon-OC-Monitor.ovl
