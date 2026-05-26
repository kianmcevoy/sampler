#!/usr/bin/env bash
# Clear the standalone sampler's persisted settings (parameter values,
# user-customized slider min/max, audio device choice, window position).
# Useful after changing slider ranges in gui/src/controls.cpp — JUCE
# restores the previous min/max from this file otherwise.
set -euo pipefail

SETTINGS="$HOME/.config/sampler.settings"

if [[ -f "$SETTINGS" ]]; then
    rm "$SETTINGS"
    echo "removed $SETTINGS"
else
    echo "no settings file at $SETTINGS — nothing to do"
fi
