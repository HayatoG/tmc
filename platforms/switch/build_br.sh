#!/usr/bin/env bash
# =============================================================================
# build_br.sh — full pipeline for the Brazilian (PT-BR) Minish Cap Switch .nro.
#
#   stage ROM  ->  extract assets (romfs_br)  ->  compile + package
#                                                 tmc_switch_br.nro
#
# The BR cart is the romsportugues "T1.0-Final" Portuguese translation: a
# USA-based romhack (game code BZME, ~617 KB different from the USA ROM). It
# therefore runs on the proven USA engine path (GAME_VER=USA in the Makefile)
# with the hack's BR-translated assets baked into romfs_br, plus a default
# PT-BR overlay (-DTMC_DEFAULT_LANG_PT). This is the build that actually runs.
#
# Run from a normal shell with Docker. Compile is delegated to devkitPro's bash.
#
#   bash platforms/switch/build_br.sh [path/to/br_rom.gba]
#
# Env overrides:
#   ZELDA_ROMS       folder holding the source ROMs (default: /d/Projects/zelda tmc)
#   DEVKITPRO_BASH   path to devkitPro's bash.exe
#   SKIP_EXTRACT=1   reuse the existing romfs_br cache, skip re-extraction
# =============================================================================
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; cd "$ROOT"

REGION=BR
DST=baserom_br.gba
EXPECT_SHA1=6838e0405c1be5d867d84e019d1f131a366b07ad
ZELDA_ROMS="${ZELDA_ROMS:-/d/Projects/zelda tmc}"
ROM_SRC="${1:-$ZELDA_ROMS/The Minish Cap (BR) (T1.0-Final) (www.romsportugues.com).gba}"
DKP_BASH="${DEVKITPRO_BASH:-/c/devkitPro/msys2/usr/bin/bash.exe}"

echo "== [BR] stage ROM =="
[ -f "$ROM_SRC" ] || { echo "ERROR: ROM not found: $ROM_SRC"; exit 1; }
cp "$ROM_SRC" "$DST"
got=$(sha1sum "$DST" | cut -d' ' -f1)
echo "  $DST  sha1=$got"
[ "$got" = "$EXPECT_SHA1" ] || echo "  WARN: sha1 != expected BR $EXPECT_SHA1 (continuing anyway)"

if [ "${SKIP_EXTRACT:-0}" = "1" ] && [ -d platforms/switch/romfs_br/assets ]; then
  echo "== [BR] SKIP_EXTRACT=1 — reusing romfs_br =="
else
  echo "== [BR] extract assets -> romfs_br =="
  bash platforms/switch/build_extractor.sh BR
fi

echo "== [BR] compile + package tmc_switch_br.nro =="
"$DKP_BASH" -lc "cd '$ROOT' && bash platforms/switch/build_region.sh BR"

echo "== [BR] DONE -> platforms/switch/tmc_switch_br.nro =="
ls -la platforms/switch/tmc_switch_br.nro
