#!/usr/bin/env bash
# Build the Minish Cap Switch homebrew. Must run inside the devkitPro MSYS2
# shell so /opt/devkitpro is mounted and DEVKITPRO is exported.
#
# From Windows you can invoke it via devkitPro's bash:
#   C:/devkitPro/msys2/usr/bin/bash.exe -lc 'cd /d/Projects/tmc-switch && bash platforms/switch/build.sh'
set -e

: "${DEVKITPRO:=/opt/devkitpro}"
export DEVKITPRO
export PATH="$DEVKITPRO/tools/bin:$DEVKITPRO/devkitA64/bin:$PATH"

cd "$(dirname "$0")"

# Refresh the source list from xmake.lua (keeps Switch + PC in sync).
python3 gen_sources.py 2>/dev/null || python gen_sources.py

make -j"$(nproc)" "$@"
