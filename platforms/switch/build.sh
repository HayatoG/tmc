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

# The Switch build compiles the ViruaPPU submodule working tree directly (unlike
# the xmake PC build, it does NOT auto-apply port/patches/viruappu-*.patch).
# Apply the Switch parallel-scanline-render patch into the pinned submodule,
# idempotently, so a fresh checkout reproduces the multithreaded PPU. The parent
# repo ignores submodule dirty state (.gitmodules), so this leaves no tracked
# change. Absolute path: `git -C` cd's into the submodule first.
ROOT="$(cd ../.. && pwd)"
PR_PATCH="$ROOT/port/patches/switch-parallel-render.patch"
if [ -f "$PR_PATCH" ]; then
    if git -C "$ROOT/libs/ViruaPPU" apply --reverse --check "$PR_PATCH" 2>/dev/null; then
        echo "[patch] switch-parallel-render already applied"
    elif git -C "$ROOT/libs/ViruaPPU" apply "$PR_PATCH" 2>/dev/null; then
        echo "[patch] applied switch-parallel-render"
    else
        echo "[patch] WARN: switch-parallel-render did not apply cleanly"
    fi
fi

# Refresh the source list from xmake.lua (keeps Switch + PC in sync).
python3 gen_sources.py 2>/dev/null || python gen_sources.py

make -j"$(nproc)" "$@"
