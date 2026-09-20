#!/bin/sh
# Run llocr from a build tree on macOS with a predictable dynamic-linker
# environment.
#
# Why: DYLD_FRAMEWORK_PATH is searched by dyld BEFORE the binary's LC_RPATH and
# even before @executable_path inside .app bundles, for any load whose path
# contains ".framework". If it points at a Homebrew Qt (e.g.
# /opt/homebrew/lib, which symlinks QtCore.framework to Cellar/qtbase/<ver>),
# the process will mix two Qt installations and crash at startup with:
#   Symbol not found: __ZN14QObjectPrivateC2E16QtPrivate_<version>
# (Qt 6 embeds its version into private symbols on purpose.)
#
# There is no point "setting" the variable here - the FIRST directory in
# DYLD_FRAMEWORK_PATH wins, and an @rpath from the binary loses regardless.
# The honest fix is to UNSET it and let the binary's own LC_RPATH resolve Qt.
# The app prints its loaded QtCore version/path at startup (see main.cpp), so
# after launching, the first "runtime Qt:" line states which Qt actually runs.
#
# Usage: scripts/run-macos.sh [--debug|--release] [args...]
# Defaults to the first existing binary: Debug, then Release.

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)

BIN=""
if [ "${1:-}" = "--debug" ]; then
    shift
    BIN="$ROOT/build/Qt_6_10_3_for_macOS_Debug/bin/llocr"
elif [ "${1:-}" = "--release" ]; then
    shift
    BIN="$ROOT/build/Qt_6_10_3_for_macOS_Release/bin/llocr"
fi

if [ -z "$BIN" ]; then
    for cand in \
        "$ROOT/build/Qt_6_10_3_for_macOS_Debug/bin/llocr" \
        "$ROOT/build/Qt_6_10_3_for_macOS_Release/bin/llocr" \
        "$ROOT/build/bin/llocr"; do
        if [ -x "$cand" ]; then
            BIN="$cand"
            break
        fi
    done
fi

if [ -z "$BIN" ] || [ ! -x "$BIN" ]; then
    echo "run-macos.sh: llocr binary not found. Build first (see AGENTS.md)." >&2
    exit 1
fi

# Also drop DYLD_LIBRARY_PATH: it has the same class of hazard (searched before
# RPATH for non-framework libraries). The binary links Qt via @rpath.
exec env -u DYLD_FRAMEWORK_PATH -u DYLD_LIBRARY_PATH "$BIN" "$@"