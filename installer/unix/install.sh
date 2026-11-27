#!/usr/bin/env bash
# Cross-platform Linux & macOS installer for OpenRaR.
# Usage:
#   ./install.sh                  # Installs to /usr/local (root) or ~/.local (non-root)
#   ./install.sh --prefix /opt/openrar
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ "$(id -u)" -eq 0 ]; then
    PREFIX="/usr/local"
else
    PREFIX="$HOME/.local"
fi

while [ "$#" -gt 0 ]; do
    case "$1" in
        --prefix)
            PREFIX="$2"
            shift 2
            ;;
        --user)
            PREFIX="$HOME/.local"
            shift
            ;;
        --system)
            PREFIX="/usr/local"
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [--prefix DIR | --user | --system]"
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
    esac
done

find_file() {
    for p in "$@"; do
        if [ -f "$p" ]; then
            echo "$p"
            return 0
        fi
    done
    return 1
}

REPO_ROOT="$(cd "$SCRIPT_DIR/../.." 2>/dev/null && pwd || echo "$SCRIPT_DIR")"
BIN_CLI="$(find_file "$SCRIPT_DIR/openrar" "$SCRIPT_DIR/bin/openrar" "$REPO_ROOT/build/openrar" || true)"
BIN_SFX="$(find_file "$SCRIPT_DIR/openrar_sfx_stub" "$SCRIPT_DIR/bin/openrar_sfx_stub" "$REPO_ROOT/build/openrar_sfx_stub" || true)"
ICON_SVG="$(find_file "$SCRIPT_DIR/openrar.svg" "$SCRIPT_DIR/share/icons/hicolor/scalable/apps/openrar.svg" "$REPO_ROOT/assets/openrar.svg" || true)"

if [ -z "$BIN_CLI" ]; then
    echo "Error: openrar binary not found. Run ./build.sh first or run install.sh from a release tarball." >&2
    exit 1
fi

echo "Installing OpenRaR to $PREFIX ..."
mkdir -p "$PREFIX/bin"
install -m 755 "$BIN_CLI" "$PREFIX/bin/openrar"
if [ -n "$BIN_SFX" ]; then
    install -m 755 "$BIN_SFX" "$PREFIX/bin/openrar_sfx_stub"
fi

# Install icon and desktop entry on Linux
if [ "$(uname -s)" = "Linux" ]; then
    if [ -n "$ICON_SVG" ]; then
        mkdir -p "$PREFIX/share/icons/hicolor/scalable/apps"
        install -m 644 "$ICON_SVG" "$PREFIX/share/icons/hicolor/scalable/apps/openrar.svg"
    fi
    mkdir -p "$PREFIX/share/applications"
    cat > "$PREFIX/share/applications/openrar.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=OpenRaR
Comment=Open-Source Cross-Platform Archiver
Exec=$PREFIX/bin/openrar
Icon=openrar
Terminal=true
Categories=Utility;Archiving;Compression;
MimeType=application/zip;application/x-tar;application/gzip;application/x-7z-compressed;application/vnd.rar;
EOF
fi

echo "Installed successfully:"
echo "  CLI: $PREFIX/bin/openrar"
if [ -n "$BIN_SFX" ]; then
    echo "  SFX: $PREFIX/bin/openrar_sfx_stub"
fi

case ":$PATH:" in
    *":$PREFIX/bin:"*) ;;
    *)
        echo ""
        echo "Note: $PREFIX/bin is not in your PATH. Add this line to ~/.bashrc or ~/.zshrc:"
        echo "  export PATH=\"$PREFIX/bin:\$PATH\""
        ;;
esac
