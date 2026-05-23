#!/usr/bin/env bash
# Build the standalone asset extractor and pre-bake the Switch asset cache.
#
# Why: on-device extraction is single-threaded (devkitA64 std::thread is
# unreliable) so it takes many minutes and looks frozen. Instead we run the
# extractor on PC (fast, threaded) and ship the resulting assets/*.pak so the
# Switch skips extraction. There's no host C++ toolchain here, so we build and
# run inside a Docker `gcc` container. The asset cache is platform-independent.
#
# Output lands in platforms/switch/dist/switch/tmc/{assets,sounds.json}.
# Run from the repo root:  bash platforms/switch/build_extractor.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
IMG=gcc:14
DIST="platforms/switch/dist/switch/tmc"

# 1. Generate the bin2c sounds blob the extractor embeds.
mkdir -p _extbuild/inc
python platforms/switch/gen_sounds_header.py 2>/dev/null || python - <<'PY'
data=open('assets/sounds.json','rb').read()
open('_extbuild/sounds.json.h','w').write(','.join('0x%02x'%b for b in data))
PY
cp -r platforms/switch/compat/fmt _extbuild/inc/fmt
cp -r platforms/switch/compat/nlohmann _extbuild/inc/nlohmann

# 2. Build + run the extractor in a Linux container.
cp "$DIST/baserom.gba" _extout/baserom.gba 2>/dev/null || { mkdir -p _extout; cp "$DIST/baserom.gba" _extout/baserom.gba; }
MSYS_NO_PATHCONV=1 docker run --rm -v "$ROOT":/work -w //work "$IMG" bash -c '
  set -e
  mkdir -p _extout
  # port_asset_index.c must be compiled as C (its symbols are extern "C").
  gcc -std=gnu11 -O2 -DPC_PORT -DNON_MATCHING -DUSA -DENGLISH -DREVISION=0 \
    -Iinclude -Iport -I. -c port/port_asset_index.c -o _extout/port_asset_index.o
  g++ -std=c++20 -O2 -DPC_PORT -DNON_MATCHING -DUSA -DENGLISH -DREVISION=0 -DFMT_HEADER_ONLY=1 \
    -Itools/src/assets_extractor -Iinclude -Iport -I. -I_extbuild/inc -I_extbuild \
    tools/src/assets_extractor/assets_extractor_main.cpp \
    tools/src/assets_extractor/assets_extractor_api.cpp \
    tools/src/assets_extractor/embedded_sounds_json.cpp \
    port/port_asset_pipeline.cpp port/port_asset_log.cpp port/port_asset_pak.cpp \
    _extout/port_asset_index.o -lpthread -o _extout/asset_extractor
  cd _extout && ./asset_extractor --pak --runtime-only --force
'

# 3. Neutralize the recorded ROM mtime so the Switch accepts the cache
#    regardless of baserom.gba's mtime on the SD card (rom_size + pack_format
#    still gate it).
python - <<'PY'
import json
p='_extout/assets/.asset_build_state.json'
s=json.load(open(p)); s['rom_mtime']=0; json.dump(s, open(p,'w'))
print('rom_mtime -> 0')
PY

# 4. Stage into the dist package.
rm -rf "$DIST/assets"
cp -r _extout/assets "$DIST/assets"
cp _extout/sounds.json "$DIST/sounds.json"
echo "Pre-baked cache staged in $DIST"

# 5. Stage into the romfs dir so `make` bundles the cache INTO the .nro
#    (self-contained build — user only supplies baserom.gba).
ROMFS="platforms/switch/romfs"
rm -rf "$ROMFS/assets"
mkdir -p "$ROMFS/assets"
cp -r _extout/assets/. "$ROMFS/assets/"
cp _extout/sounds.json "$ROMFS/sounds.json"
echo "Romfs cache staged in $ROMFS (rebuild the .nro to embed it)"
