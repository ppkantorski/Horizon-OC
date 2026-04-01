#!/bin/sh

SRC="Source/Atmosphere/stratosphere/loader/source/oc"
DEST="build/stratosphere/loader/source/oc"
mkdir -p "dist/atmosphere/kips/"
mkdir -p "$DEST"

cp -r "$SRC"/. "$DEST"/

cd build/stratosphere/loader || exit 1
make -j"$(nproc)"
hactool -t kip1 out/nintendo_nx_arm64_armv8a/release/loader.kip --uncompress=hoc.kip
cd ../../../ # exit
cp build/stratosphere/loader/hoc.kip dist/atmosphere/kips/hoc.kip

cd Source/hoc-clk/
./build.sh
cp -r dist/ ../../

cd ../../

cd Source/Horizon-OC-Monitor/
make -j"$(nproc)"
cp Horizon-OC-Monitor.ovl ../../dist/switch/.overlays/Horizon-OC-Monitor.ovl