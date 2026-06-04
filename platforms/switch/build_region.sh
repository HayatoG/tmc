#!/usr/bin/env bash
# Build ONE region's Switch .nro. Must run inside the devkitPro MSYS2 shell
# (so /opt/devkitpro is mounted and the devkitA64 toolchain is on PATH).
#
#   C:/devkitPro/msys2/usr/bin/bash.exe -lc \
#     'cd /d/Projects/tmc-switch && bash platforms/switch/build_region.sh EU'
#
# REGION is one of USA | EU | BR (default USA). Produces:
#   platforms/switch/tmc_switch_<region>.nro  (+ its embedded romfs_<region>)
#
# This mirrors build.sh but (a) forwards REGION to make and (b) tolerates a
# devkitPro shell without Python — sources.mk is committed and kept in sync, so
# the gen_sources refresh is best-effort, not fatal.
set -e

: "${DEVKITPRO:=/opt/devkitpro}"
export DEVKITPRO
export PATH="$DEVKITPRO/tools/bin:$DEVKITPRO/devkitA64/bin:$PATH"

cd "$(dirname "$0")"

REGION="${1:-USA}"
[ $# -gt 0 ] && shift || true

ROOT="$(cd ../.. && pwd)"

# Apply the Switch parallel-scanline-render patch into the pinned ViruaPPU
# submodule, idempotently (same as build.sh). The parent repo ignores submodule
# dirty state, so this leaves no tracked change.
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

# Best-effort source-list refresh from xmake.lua (keeps Switch + PC in sync).
# Non-fatal: the committed sources.mk is authoritative when Python is absent.
if command -v python3 >/dev/null 2>&1; then
    python3 gen_sources.py || echo "[sources] gen_sources failed — using committed sources.mk"
elif command -v python >/dev/null 2>&1; then
    python gen_sources.py || echo "[sources] gen_sources failed — using committed sources.mk"
else
    echo "[sources] no Python in this shell — using committed sources.mk"
fi

echo "[build] REGION=$REGION  ->  tmc_switch_$(echo "$REGION" | tr 'A-Z' 'a-z').nro"
make -j"$(nproc)" REGION="$REGION" "$@"
