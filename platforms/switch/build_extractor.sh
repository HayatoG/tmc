#!/usr/bin/env bash
# Build the standalone asset extractor and pre-bake the Switch asset cache,
# PER REGION.
#
# Why: on-device extraction is single-threaded (devkitA64 std::thread is
# unreliable) so it takes many minutes and looks frozen. Instead we run the
# extractor on PC (fast, threaded) and ship the resulting assets/*.pak so the
# Switch skips extraction. There's no host C++ toolchain here, so we build and
# run inside a Docker `gcc` container. The asset cache is platform-independent.
#
# The extractor auto-detects the ROM region from the header game code
# (0xAC: "BZME" = USA, "BZMP" = EU; the Brazilian cart is byte-identical to EU)
# and applies the matching root-table offsets — see ExtractAll() in
# tools/src/assets_extractor/assets_extractor_api.cpp. Nothing is shared
# between regions: each ROM is extracted independently into its own cache.
#
# Usage (from repo root):
#     bash platforms/switch/build_extractor.sh [REGION...]
#   REGION is one or more of: USA EU BR   (default: USA)
#   e.g.  bash platforms/switch/build_extractor.sh USA EU BR
#
# Per region it reads the staged ROM at the repo root:
#     USA -> baserom.gba      EU -> baserom_eu.gba      BR -> baserom_br.gba
# and writes a self-contained cache to:
#     platforms/switch/romfs_<region>/{assets,sounds.json}
# which the per-region Makefile (GAME_VER=<region>) bundles into the .nro.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
IMG=gcc:14

REGIONS=("$@")
[ ${#REGIONS[@]} -eq 0 ] && REGIONS=(USA)

rom_for() {
  case "$1" in
    USA) echo "baserom.gba" ;;
    BR)  echo "baserom_br.gba" ;;
    *)   echo "" ;;
  esac
}

# Validate every requested region + ROM up front.
SPECS=""
for r in "${REGIONS[@]}"; do
  rom="$(rom_for "$r")"
  if [ -z "$rom" ]; then echo "ERROR: unknown region '$r' (use USA|EU|BR)"; exit 1; fi
  if [ ! -f "$rom" ]; then echo "ERROR: missing $rom for region $r (stage it at the repo root)"; exit 1; fi
  SPECS="$SPECS ${r}:${rom}"
done
echo "Regions to extract:${SPECS}"

# 1. Generate the bin2c sounds blob the extractor embeds (perl — no Python on
#    this host). Skip if it already exists and is non-empty.
mkdir -p _extbuild/inc _extout
if [ ! -s _extbuild/sounds.json.h ]; then
  perl -e 'local $/; open(F, "<:raw", $ARGV[0]) or die $!; my $d = <F>;
           print join(",", map { sprintf("0x%02x", $_) } unpack("C*", $d));' \
       assets/sounds.json > _extbuild/sounds.json.h
fi
cp -r platforms/switch/compat/fmt _extbuild/inc/fmt
cp -r platforms/switch/compat/nlohmann _extbuild/inc/nlohmann

# 2. Build the extractor ONCE, then run it once per region in a Linux container.
#    Each region gets its own working dir so executable_dir (and therefore the
#    baserom.gba it reads + the assets/ it writes) is region-private.
MSYS_NO_PATHCONV=1 docker run --rm -v "$ROOT":/work -w //work "$IMG" bash -c '
  set -e
  mkdir -p _extout
  # port_asset_index.c (the per-asset ROM offsets) must be compiled as C — its
  # symbols are extern "C". USA and the BR hack are both game code BZME, so one
  # USA-built extractor serves both (the extractor also auto-detects the ROM
  # region at runtime for the root-table offsets).
  gcc -std=gnu11 -O2 -DPC_PORT -DNON_MATCHING -DUSA -DENGLISH -DREVISION=0 \
    -Iinclude -Iport -I. -c port/port_asset_index.c -o _extout/port_asset_index.o
  g++ -std=c++20 -O2 -DPC_PORT -DNON_MATCHING -DUSA -DENGLISH -DREVISION=0 -DFMT_HEADER_ONLY=1 \
    -Itools/src/assets_extractor -Iinclude -Iport -I. -I_extbuild/inc -I_extbuild \
    tools/src/assets_extractor/assets_extractor_main.cpp \
    tools/src/assets_extractor/assets_extractor_api.cpp \
    tools/src/assets_extractor/embedded_sounds_json.cpp \
    port/port_asset_pipeline.cpp port/port_asset_log.cpp port/port_asset_pak.cpp \
    _extout/port_asset_index.o -lpthread -o _extout/asset_extractor

  for spec in '"$SPECS"'; do
    region="${spec%%:*}"; rom="${spec##*:}"
    out="_extout/${region}"
    echo "=== extracting ${region} from ${rom} -> ${out} ==="
    rm -rf "$out"; mkdir -p "$out"
    cp "_extout/asset_extractor" "$out/asset_extractor"
    cp "$rom" "$out/baserom.gba"
    ( cd "$out" && ./asset_extractor --pak --runtime-only --force )
  done
'

# 3. Per region: neutralize the recorded ROM mtime so the Switch accepts the
#    cache regardless of baserom mtime on the SD card (rom_size + pack_format
#    still gate it), then stage into the region-private romfs dir.
for spec in $SPECS; do
  region="${spec%%:*}"
  out="_extout/${region}"
  lc="$(echo "$region" | tr 'A-Z' 'a-z')"
  romfs="platforms/switch/romfs_${lc}"

  state="$out/assets/.asset_build_state.json"
  perl -0777 -i -pe 's/("rom_mtime"\s*:\s*)-?\d+/${1}0/' "$state"
  echo "rom_mtime -> 0 for $state"

  rm -rf "$romfs"
  mkdir -p "$romfs/assets"
  cp -r "$out/assets/." "$romfs/assets/"
  cp "$out/sounds.json" "$romfs/sounds.json"
  echo "Region ${region}: cache staged in ${romfs} (rebuild the .nro to embed it)"
done

echo "Done. Build NROs with:  bash platforms/switch/build.sh GAME_VER=<USA|EU|BR>"
