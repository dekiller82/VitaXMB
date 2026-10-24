#!/bin/bash
# Builds the PSP launcher (needs the pspdev toolchain; see README.md). Run inside WSL / Linux:  bash psp/launcher/build.sh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/env.sh"
BUILD=/tmp/vxmbboot-build
rm -rf "$BUILD" && mkdir -p "$BUILD"
cp "$HERE/main.c" "$HERE/Makefile" "$BUILD/"
cd "$BUILD"
make 2>&1 | tail -20
ls -la EBOOT.PBP
cp EBOOT.PBP "$HERE/../../assets/psp/vxmbboot.pbp"
