#!/usr/bin/env bash
#
# Build a self-contained QtEdit AppImage.
#
# This bundles Qt and the platform plugins so the result runs without Qt
# development packages. It downloads linuxdeploy and its Qt plugin into the
# user's cache on first use.
#
# Usage: packaging/build-appimage.sh
#
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

VERSION="$(sed -n 's/^project(QtEdit VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
[ -n "$VERSION" ] || VERSION="1.0.0"

case "$(uname -m)" in
    x86_64|amd64)   ARCH="x86_64" ;;
    aarch64|arm64)  ARCH="aarch64" ;;
    i686|i386)      ARCH="i686" ;;
    *)              echo "unsupported architecture: $(uname -m)" >&2; exit 1 ;;
esac

# Pinned linuxdeploy releases. Override with environment variables if needed.
LINUXDEPLOY_VERSION="${LINUXDEPLOY_VERSION:-1-alpha-20251107-1}"
QT_PLUGIN_VERSION="${QT_PLUGIN_VERSION:-1-alpha-20250213-1}"

CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/qtedit-appimage"
mkdir -p "$CACHE"

log() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

need() {
    command -v "$1" >/dev/null 2>&1 || die "required tool not found: $1"
}

need cmake
need curl
need install

QMAKE_BIN="$(command -v qmake6 || command -v qmake || true)"
[ -n "$QMAKE_BIN" ] || die "qmake6 not found; install Qt 6 development files"
if ! "$QMAKE_BIN" -query QT_VERSION | grep -E '^6\.' >/dev/null; then
    die "$QMAKE_BIN does not belong to Qt 6"
fi

download_tool() {
    local url="$1"
    local dest="$2"

    if [ ! -x "$dest" ]; then
        log "Downloading $(basename "$dest")"
        curl -fL --retry 3 --connect-timeout 20 -o "$dest.part" "$url"
        mv "$dest.part" "$dest"
        chmod +x "$dest"
    fi
}

LINUXDEPLOY="$CACHE/linuxdeploy-$ARCH.AppImage"
QT_PLUGIN="$CACHE/linuxdeploy-plugin-qt-$ARCH.AppImage"
APPIMAGETOOL="$CACHE/appimagetool-$ARCH.AppImage"

download_tool \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/$LINUXDEPLOY_VERSION/linuxdeploy-$ARCH.AppImage" \
    "$LINUXDEPLOY"
download_tool \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/$QT_PLUGIN_VERSION/linuxdeploy-plugin-qt-$ARCH.AppImage" \
    "$QT_PLUGIN"
download_tool \
    "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-$ARCH.AppImage" \
    "$APPIMAGETOOL"

# linuxdeploy finds plugins next to its own binary or in PATH.
ln -sf "$QT_PLUGIN" "$CACHE/linuxdeploy-plugin-qt"
export PATH="$CACHE:$PATH"

# Some distributions ship optional Qt plugins with missing dependencies
# (e.g. KDE's JPEG-XR image plugin). Build a curated plugin tree, skipping
# any plugin that cannot resolve all of its libraries, and point a qmake
# wrapper at it.
QT_PLUGINS_ORIG="$("$QMAKE_BIN" -query QT_INSTALL_PLUGINS)"
QT_PLUGINS_CURATED="$CACHE/qtplugins"
rm -rf "$QT_PLUGINS_CURATED"
mkdir -p "$QT_PLUGINS_CURATED"

while IFS= read -r -d '' plugin; do
    rel="${plugin#"$QT_PLUGINS_ORIG"/}"
    mkdir -p "$QT_PLUGINS_CURATED/$(dirname "$rel")"
    ln -s "$plugin" "$QT_PLUGINS_CURATED/$rel"
done < <(find "$QT_PLUGINS_ORIG" -type f -print0)

while IFS= read -r -d '' plugin; do
    if ldd "$plugin" 2>/dev/null | grep -F 'not found' >/dev/null; then
        echo "Skipping Qt plugin with missing dependencies: ${plugin#"$QT_PLUGINS_CURATED"/}"
        rm -f "$plugin"
    fi
done < <(find "$QT_PLUGINS_CURATED" -type l -name '*.so' -print0)

QMAKE_WRAPPER="$CACHE/qmake-qtedit"
cat > "$QMAKE_WRAPPER" <<EOF
#!/bin/sh
if [ "\$#" -eq 1 ] && [ "\$1" = "-query" ]; then
    "$QMAKE_BIN" -query | sed 's|^QT_INSTALL_PLUGINS:.*|QT_INSTALL_PLUGINS:$QT_PLUGINS_CURATED|'
    exit 0
fi
exec "$QMAKE_BIN" "\$@"
EOF
chmod +x "$QMAKE_WRAPPER"

# The Wayland platform plugin changed names between Qt versions. Deploy
# whichever variants this Qt installation provides.
EXTRA_PLATFORMS=""
for platform in libqwayland.so libqwayland-egl.so libqwayland-generic.so libqoffscreen.so; do
    if [ -f "$QT_PLUGINS_ORIG/platforms/$platform" ]; then
        EXTRA_PLATFORMS="${EXTRA_PLATFORMS:+$EXTRA_PLATFORMS;}$platform"
    fi
done

BUILD_DIR="$ROOT/build-appimage"
APPDIR="$ROOT/dist/QtEdit.AppDir"
OUTPUT="QtEdit-$VERSION-$ARCH.AppImage"

log "Building QtEdit $VERSION for $ARCH"
cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --target qtedit --parallel "$( (command -v nproc >/dev/null && nproc) || echo 2 )"

rm -rf "$APPDIR"
install -Dm755 "$BUILD_DIR/qtedit" "$APPDIR/usr/bin/qtedit"
install -Dm644 "$ROOT/qtedit.desktop" \
    "$APPDIR/usr/share/applications/qtedit.desktop"
install -Dm644 "$ROOT/resources/appicon.svg" \
    "$APPDIR/usr/share/icons/hicolor/scalable/apps/qtedit.svg"
install -Dm644 "$ROOT/packaging/io.github.gtedit.QtEdit.metainfo.xml" \
    "$APPDIR/usr/share/metainfo/io.github.gtedit.QtEdit.metainfo.xml"

# Make linuxdeploy's AppDir deployment deterministic and avoid FUSE issues
# when the tools are themselves AppImages.
export APPIMAGE_EXTRACT_AND_RUN=1
# linuxdeploy's bundled binutils may be too old for modern RELR sections.
export NO_STRIP=1
export QMAKE="$QMAKE_WRAPPER"
# Widgets need X11 and Wayland platforms. The offscreen plugin is useful for
# headless verification and remote/CI use. QtSvg provides the SVG icon engine.
export EXTRA_PLATFORM_PLUGINS="$EXTRA_PLATFORMS"
export EXTRA_QT_MODULES="svg"
export OUTPUT

log "Bundling Qt libraries and plugins"
( cd "$ROOT/dist" && "$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/qtedit" \
    --desktop-file "$APPDIR/usr/share/applications/qtedit.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/scalable/apps/qtedit.svg" \
    --plugin qt )

# linuxdeploy-plugin-qt deploys the Wayland platform plugin but not Qt's
# Wayland shell and client-buffer integration plugins. Without those, Qt
# aborts on Wayland sessions with "No shell integration named xdg-shell
# found" and "Failed to load client buffer integration". Copy the curated
# variants and bundle any Qt libraries they need.
for plugin_dir in \
    wayland-decoration-client \
    wayland-graphics-integration-client \
    wayland-shell-integration
do
    src="$QT_PLUGINS_CURATED/$plugin_dir"
    [ -d "$src" ] || continue
    dest="$APPDIR/usr/plugins/$plugin_dir"
    mkdir -p "$dest"
    while IFS= read -r -d '' plugin; do
        cp -L "$plugin" "$dest/"
    done < <(find "$src" -maxdepth 1 -type l -name '*.so' -print0)
done

while IFS= read -r -d '' plugin; do
    ldd "$plugin" 2>/dev/null | awk '/=> \// { print $3 }'
done < <(find "$APPDIR/usr/plugins" \( -path '*/wayland-*/*.so' \) -type f -print0) |
while IFS= read -r lib; do
    base="$(basename "$lib")"
    case "$base" in
        libQt6*.so.*) ;;
        *) continue ;;
    esac
    [ -e "$APPDIR/usr/lib/$base" ] && continue
    real="$(readlink -f "$lib")"
    install -Dm755 "$real" "$APPDIR/usr/lib/$(basename "$real")"
    ln -sf "$(basename "$real")" "$APPDIR/usr/lib/$base"
done

for plugin_dir in wayland-graphics-integration-client wayland-shell-integration; do
    [ -d "$APPDIR/usr/plugins/$plugin_dir" ] \
        || die "failed to bundle Qt $plugin_dir plugins"
done

# linuxdeploy's generated AppRun execs an "AppRun.wrapped" symlink. Qt then
# resolves qt.conf and the plugin paths relative to that symlink instead of
# usr/bin, which makes the AppImage abort on startup. Launch the real binary
# by path instead.
cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/sh
set -e
this_dir="$(dirname "$(readlink -f "$0")")"

if [ -f "$this_dir/apprun-hooks/linuxdeploy-plugin-qt-hook.sh" ]; then
    . "$this_dir/apprun-hooks/linuxdeploy-plugin-qt-hook.sh"
fi

exec "$this_dir/usr/bin/qtedit" "$@"
EOF
chmod 755 "$APPDIR/AppRun"

log "Creating the AppImage"
( cd "$ROOT/dist" && ARCH="$ARCH" "$APPIMAGETOOL" "$APPDIR" "$ROOT/dist/$OUTPUT" )

# linuxdeploy names its output after the desktop file unless OUTPUT is
# honored; normalize the name either way.
if [ ! -f "$ROOT/dist/$OUTPUT" ]; then
    generated="$(find "$ROOT/dist" -maxdepth 1 -name '*.AppImage' -newer "$APPDIR/usr/bin/qtedit" -print -quit)"
    [ -n "$generated" ] || die "linuxdeploy did not produce an AppImage"
    mv "$generated" "$ROOT/dist/$OUTPUT"
fi

chmod +x "$ROOT/dist/$OUTPUT"

log "Verifying the AppImage"
if ! APPIMAGE_EXTRACT_AND_RUN=1 QT_QPA_PLATFORM=offscreen \
     "$ROOT/dist/$OUTPUT" --version; then
    die "the generated AppImage failed to start"
fi

GLIBC_REQ="$(
    find "$APPDIR" -type f -print0 \
        | while IFS= read -r -d '' file; do
              if file -b "$file" | grep -E '^ELF' >/dev/null; then
                  objdump -T "$file" 2>/dev/null
              fi
          done \
        | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -n1 || true
)"

printf '\n'
log "Created: $ROOT/dist/$OUTPUT"
if [ -n "$GLIBC_REQ" ]; then
    printf '  Minimum glibc: %s\n' "$GLIBC_REQ"
    if printf '%s\n' "$GLIBC_REQ" | awk -F_ '{split($2,v,"."); if (v[1]>2 || (v[1]==2 && v[2]>35)) exit 0; exit 1}'; then
        printf '  Note: this AppImage inherits the build host glibc. Build it on\n'
        printf '        Ubuntu 22.04 or older for broad compatibility (see\n'
        printf '        .github/workflows/appimage.yml).\n'
    fi
fi
printf '  Install it with: ./install.sh --appimage %s\n' "$ROOT/dist/$OUTPUT"
