#!/usr/bin/env bash
# =============================================================================
# build_usa.sh — full pipeline for the USA Minish Cap Switch .nro.
#
#   stage ROM  ->  extract assets (romfs_usa)  ->  compile + package
#                                                  tmc_switch_usa.nro
#
# USA cart, game code BZME. The engine is built for USA (GAME_VER=USA) — the
# fully-working path.
#
# Run from a normal shell that has Docker (the extractor builds/runs in a gcc
# container). The compile step is delegated to the devkitPro MSYS2 bash.
#
#   bash platforms/switch/build_usa.sh [path/to/usa_rom.gba]
#
# Env overrides:
#   ZELDA_ROMS       folder holding the source ROMs (default: /d/Projects/zelda tmc)
#   DEVKITPRO_BASH   path to devkitPro's bash.exe
#   SKIP_EXTRACT=1   reuse the existing romfs_usa cache, skip re-extraction
# =============================================================================
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; cd "$ROOT"

REGION=USA
DST=baserom.gba
EXPECT_SHA1=b4bd50e4131b027c334547b4524e2dbbd4227130
ZELDA_ROMS="${ZELDA_ROMS:-/d/Projects/zelda tmc}"
ROM_SRC="${1:-$ZELDA_ROMS/Legend of Zelda, The - The Minish Cap (USA).gba}"
DKP_BASH="${DEVKITPRO_BASH:-/c/devkitPro/msys2/usr/bin/bash.exe}"

echo "== [USA] stage ROM =="
[ -f "$ROM_SRC" ] || { echo "ERROR: ROM not found: $ROM_SRC"; exit 1; }
cp "$ROM_SRC" "$DST"
got=$(sha1sum "$DST" | cut -d' ' -f1)
echo "  $DST  sha1=$got"
[ "$got" = "$EXPECT_SHA1" ] || echo "  WARN: sha1 != expected USA $EXPECT_SHA1 (continuing anyway)"

if [ "${SKIP_EXTRACT:-0}" = "1" ] && [ -d platforms/switch/romfs_usa/assets ]; then
  echo "== [USA] SKIP_EXTRACT=1 — reusing romfs_usa =="
else
  echo "== [USA] extract assets -> romfs_usa =="
  bash platforms/switch/build_extractor.sh USA
fi

echo "== [USA] compile + package tmc_switch_usa.nro =="
"$DKP_BASH" -lc "cd '$ROOT' && bash platforms/switch/build_region.sh USA"

echo "== [USA] DONE -> platforms/switch/tmc_switch_usa.nro =="
ls -la platforms/switch/tmc_switch_usa.nro
