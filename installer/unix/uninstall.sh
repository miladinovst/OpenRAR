#!/usr/bin/env bash
# Cross-platform Linux & macOS uninstaller for OpenRAR.
set -euo pipefail

if [ "$(id -u)" -eq 0 ]; then
    PREFIX="/usr/local"
else
    PREFIX="$HOME/.local"
fi

if [ "${1:-}" = "--prefix" ] && [ -n "${2:-}" ]; then
    PREFIX="$2"
elif [ "${1:-}" = "--user" ]; then
    PREFIX="$HOME/.local"
elif [ "${1:-}" = "--system" ]; then
    PREFIX="/usr/local"
fi

echo "Removing OpenRAR from $PREFIX ..."
rm -f "$PREFIX/bin/openrar"
rm -f "$PREFIX/bin/openrar_sfx_stub"
rm -f "$PREFIX/share/icons/hicolor/scalable/apps/openrar.svg"
rm -f "$PREFIX/share/applications/openrar.desktop"
echo "OpenRAR uninstalled."
