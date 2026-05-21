#!/usr/bin/env bash
# Copy the built VST3 to the local Instruo plugins directory.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$SCRIPT_DIR/build/sampler_artefacts/Debug/VST3/sampler.vst3"
DEST_DIR="$HOME/instruo/plugins"

if [[ ! -d "$SRC" ]]; then
    echo "error: VST3 not found at $SRC — build the project first" >&2
    exit 1
fi

mkdir -p "$DEST_DIR"
rm -rf "$DEST_DIR/sampler.vst3"
cp -r "$SRC" "$DEST_DIR/"
echo "installed $SRC → $DEST_DIR/sampler.vst3"
