#!/usr/bin/env bash
#
# QtEdit universal installer
#
# Installs QtEdit for the current user (default) or system-wide, builds it
# from source when needed, and integrates it with the desktop (menu entry,
# icons, AppStream metadata, MIME associations and an uninstaller).
#
# It can also install a self-contained AppImage produced by
# packaging/build-appimage.sh, which does not require Qt development files.
#
set -euo pipefail

APP_NAME="QtEdit"
APP_ID="qtedit"
VERSION="1.0.0"
ROOT="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

MODE="user"
PREFIX=""
ACTION="install"
DO_BUILD=1
ALLOW_DEPS=1
ASSUME_YES=0
APPIMAGE=""
JOBS=""

# ---------------------------------------------------------------- utilities

log()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33mwarning:\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

usage() {
    cat <<EOF
$APP_NAME $VERSION installer

Usage: ./install.sh [options]

Options:
  --user              Install for the current user in ~/.local (default)
  --system            Install system-wide in /usr/local (uses sudo)
  --prefix DIR        Install under DIR instead of the default prefix
  --appimage FILE     Install from a prebuilt AppImage (no build required)
  --no-build          Use an existing build/qtedit instead of rebuilding
  --no-deps           Never try to install missing build dependencies
  -y, --yes           Do not ask for confirmation
  -j, --jobs N        Parallel build jobs (default: number of CPUs)
  --uninstall         Remove a previous installation from the chosen prefix
  -h, --help          Show this help

Examples:
  ./install.sh                          # per-user install, integrated
  ./install.sh --system                 # system-wide install
  sudo ./install.sh --uninstall         # remove a system-wide install
  ./install.sh --appimage QtEdit.AppImage

The installed launcher is available as "$APP_ID". A menu entry and icons
are installed as well, and an uninstaller is placed next to the launcher.
EOF
}

# ---------------------------------------------------------------- arguments

while [ $# -gt 0 ]; do
    case "$1" in
        --user)      MODE="user" ;;
        --system)    MODE="system" ;;
        --prefix)    [ $# -ge 2 ] || die "--prefix needs a directory"; PREFIX="$2"; shift ;;
        --appimage)  [ $# -ge 2 ] || die "--appimage needs a file"; APPIMAGE="$2"; DO_BUILD=0; shift ;;
        --no-build)  DO_BUILD=0 ;;
        --no-deps)   ALLOW_DEPS=0 ;;
        -y|--yes)    ASSUME_YES=1 ;;
        -j|--jobs)   [ $# -ge 2 ] || die "--jobs needs a number"; JOBS="$2"; shift ;;
        --uninstall) ACTION="uninstall" ;;
        -h|--help)   usage; exit 0 ;;
        *)           die "unknown option: $1 (try --help)" ;;
    esac
    shift
done

if [ -z "$PREFIX" ]; then
    if [ "$MODE" = "system" ]; then
        PREFIX="/usr/local"
    elif [ "$ACTION" = "uninstall" ] && [ "$(id -u)" -eq 0 ] \
         && [ -f "/usr/local/share/$APP_ID/uninstall.sh" ]; then
        # "sudo ./install.sh --uninstall" should remove the system-wide copy
        # instead of looking in /root/.local.
        PREFIX="/usr/local"
    else
        PREFIX="$HOME/.local"
    fi
fi
PREFIX="${PREFIX%/}"

[ -n "$PREFIX" ] || die "empty install prefix"
case "$PREFIX" in
    /*) ;;
    *) die "--prefix must be an absolute path" ;;
esac
case "$PREFIX" in
    /) die "installing directly into / is not supported" ;;
    *"'"*|*$'\n'*) die "the install prefix may not contain single quotes or newlines" ;;
esac

if [ -z "$JOBS" ]; then
    JOBS="$( (command -v nproc >/dev/null 2>&1 && nproc) || \
              (command -v getconf >/dev/null 2>&1 && getconf _NPROCESSORS_ONLN) || echo 2 )"
fi

BIN_DIR="$PREFIX/bin"
APPS_DIR="$PREFIX/share/applications"
ICON_DIR="$PREFIX/share/icons/hicolor"
METAINFO_DIR="$PREFIX/share/metainfo"
DATA_DIR="$PREFIX/share/$APP_ID"
DESKTOP_FILE="$APPS_DIR/$APP_ID.desktop"
METAINFO_FILE="$METAINFO_DIR/io.github.gtedit.QtEdit.metainfo.xml"
UNINSTALL_FILE="$DATA_DIR/uninstall.sh"

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    if command -v sudo >/dev/null 2>&1; then
        ELEVATE="sudo"
    elif command -v doas >/dev/null 2>&1; then
        ELEVATE="doas"
    else
        ELEVATE=""
    fi
else
    ELEVATE=""
fi

if [ "$MODE" = "system" ] && [ "$(id -u)" -ne 0 ]; then
    [ -n "$ELEVATE" ] \
        || die "system-wide install needs root; run as root or install sudo/doas"
    SUDO="$ELEVATE"
fi

priv() {
    if [ -n "$SUDO" ]; then
        "$SUDO" "$@"
    else
        "$@"
    fi
}

root_priv() {
    if [ -n "$ELEVATE" ]; then
        "$ELEVATE" "$@"
    else
        "$@"
    fi
}

if [ "$MODE" = "user" ]; then
    if ! mkdir -p "$PREFIX" 2>/dev/null || [ ! -w "$PREFIX" ]; then
        die "$PREFIX is not writable; use --system or choose a writable --prefix"
    fi
fi

# ------------------------------------------------------------- uninstalling

if [ "$ACTION" = "uninstall" ]; then
    [ -f "$UNINSTALL_FILE" ] \
        || die "no QtEdit installation found in $PREFIX (expected $UNINSTALL_FILE)"
    log "Removing QtEdit from $PREFIX"
    priv sh "$UNINSTALL_FILE"
    exit 0
fi

# ------------------------------------------------------- dependency helpers

qt6_available() {
    if command -v pkg-config >/dev/null 2>&1 \
       && pkg-config --exists Qt6Widgets 2>/dev/null; then
        return 0
    fi

    local dir
    for dir in \
        /usr/lib/cmake /usr/lib64/cmake /usr/local/lib/cmake \
        /opt/qt6/lib/cmake /opt/Qt6/lib/cmake; do
        [ -f "$dir/Qt6/Qt6Config.cmake" ] && return 0
    done
    for dir in /usr/lib/*/cmake /usr/lib/*/*/cmake; do
        [ -f "$dir/Qt6/Qt6Config.cmake" ] && return 0
    done
    if [ -d "$HOME/Qt" ]; then
        for dir in "$HOME"/Qt/*/gcc_64/lib/cmake "$HOME"/Qt/*/clang_64/lib/cmake; do
            [ -f "$dir/Qt6/Qt6Config.cmake" ] && return 0
        done
    fi
    return 1
}

distro_hint() {
    if [ -e /etc/NIXOS ] || { command -v nix >/dev/null 2>&1 && [ ! -e /etc/arch-release ]; }; then
        echo "NixOS/Nix: nix-shell -p cmake qt6.qtbase gcc gnumake pkg-config --run './install.sh --no-deps'"
    fi
}

dependency_command() {
    if command -v apt-get >/dev/null 2>&1; then
        echo "apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential cmake qt6-base-dev"
    elif command -v dnf >/dev/null 2>&1; then
        echo "dnf install -y gcc-c++ cmake qt6-qtbase-devel"
    elif command -v pacman >/dev/null 2>&1; then
        echo "pacman -S --needed --noconfirm base-devel cmake qt6-base"
    elif command -v zypper >/dev/null 2>&1; then
        echo "zypper --non-interactive install gcc-c++ cmake qt6-base-devel"
    elif command -v apk >/dev/null 2>&1; then
        echo "apk add --no-cache build-base cmake qt6-qtbase-dev"
    elif command -v xbps-install >/dev/null 2>&1; then
        echo "xbps-install -Sy base-devel cmake qt6-base-devel"
    elif command -v eopkg >/dev/null 2>&1; then
        echo "eopkg install -y -c system.devel qt6-base-devel"
    elif command -v emerge >/dev/null 2>&1; then
        echo "emerge --ask=n dev-build/cmake dev-qt/qtbase"
    fi
}

install_dependencies() {
    local missing=()
    command -v cmake >/dev/null 2>&1 || missing+=("CMake")
    { command -v c++ >/dev/null 2>&1 || command -v g++ >/dev/null 2>&1 \
      || command -v clang++ >/dev/null 2>&1; } || missing+=("a C++ compiler")
    qt6_available || missing+=("Qt 6 development files (6.2 or newer)")

    [ ${#missing[@]} -eq 0 ] && return 0

    local list
    list="$(printf '%s, ' "${missing[@]}")"
    list="${list%, }"

    if [ "$ALLOW_DEPS" -eq 0 ]; then
        distro_hint
        die "missing: $list. Install them and re-run, or omit --no-deps."
    fi

    local cmd
    cmd="$(dependency_command)"
    if [ -z "$cmd" ]; then
        distro_hint
        die "missing: $list. Install CMake, a C++17 compiler and Qt 6 development files, then re-run."
    fi

    log "Missing: $list"
    log "Suggested command: $cmd"

    if [ "$ASSUME_YES" -ne 1 ]; then
        if [ ! -t 0 ]; then
            die "not running interactively; install the dependencies manually or pass --yes"
        fi
        local answer
        printf 'Install these dependencies now? [Y/n] '
        read -r answer || answer="n"
        case "$answer" in
            ""|y|Y|yes|YES) ;;
            *) die "dependencies were not installed" ;;
        esac
    fi

    log "Installing build dependencies"
    if ! root_priv bash -c "$cmd"; then
        die "dependency installation failed; run it manually and re-run this installer"
    fi

    command -v cmake >/dev/null 2>&1 || die "CMake is still missing"
    qt6_available || die "Qt 6 development files are still missing"
}

# --------------------------------------------------------------- building

# Sets BINARY to the built executable. It deliberately does not print the
# path: callers must not use command substitution, because the build and
# dependency steps log to stdout and that output would be captured together
# with the path.
build_application() {
    local candidate

    BINARY=""

    if [ "$DO_BUILD" -eq 0 ]; then
        for candidate in "$ROOT/build/qtedit" "$ROOT/build/Release/qtedit"; do
            if [ -x "$candidate" ]; then
                BINARY="$candidate"
                return 0
            fi
        done
        die "no built binary found under $ROOT/build (run without --no-build first)"
    fi

    install_dependencies

    log "Configuring the build"
    if ! cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release; then
        die "CMake configuration failed (Qt 6.2+ development files are required)"
    fi

    log "Building QtEdit ($JOBS jobs)"
    cmake --build "$ROOT/build" --target qtedit --parallel "$JOBS"

    for candidate in "$ROOT/build/qtedit" "$ROOT/build/Release/qtedit"; do
        if [ -x "$candidate" ]; then
            BINARY="$candidate"
            return 0
        fi
    done

    die "build succeeded but the qtedit binary could not be found"
}

# -------------------------------------------------------------- staging

STAGE_PARENT="${QTEDIT_TMPDIR:-${TMPDIR:-/tmp}}"
if [ "$MODE" = "user" ] && [ -d "$PREFIX" ] && [ -w "$PREFIX" ]; then
    # Keep large AppImage payloads on the destination filesystem.
    STAGE_PARENT="$PREFIX"
fi
STAGE="$(mktemp -d "$STAGE_PARENT/.qtedit-install.XXXXXX")" \
    || die "cannot create a staging directory under $STAGE_PARENT (set QTEDIT_TMPDIR)"
trap 'rm -rf "$STAGE"' EXIT
STAGE_PREFIX="$STAGE/prefix"
mkdir -p "$STAGE_PREFIX/bin" "$STAGE_PREFIX/share"

stage_source_install() {
    local binary="$1"
    mkdir -p "$STAGE_PREFIX/bin"
    install -m755 "$binary" "$STAGE_PREFIX/bin/$APP_ID"
}

stage_appimage_install() {
    local image="$1"

    [ -f "$image" ] || die "AppImage not found: $image"
    if [ ! -x "$image" ]; then
        chmod +x "$image" 2>/dev/null || die "cannot make AppImage executable: $image"
    fi

    local extract="$STAGE/appimage"
    mkdir -p "$extract"
    log "Extracting AppImage"
    if ! ( cd "$extract" && "$image" --appimage-extract >/dev/null ); then
        die "failed to extract $image (is it a valid AppImage?)"
    fi
    [ -d "$extract/squashfs-root" ] || die "AppImage extraction produced no files"

    local app_dest="$STAGE_PREFIX/share/$APP_ID/app"
    mkdir -p "$(dirname "$app_dest")"
    mv "$extract/squashfs-root" "$app_dest"

    if [ ! -x "$app_dest/AppRun" ]; then
        warn "AppImage has no AppRun; using the bundled binary directly"
    fi
    if [ ! -x "$app_dest/AppRun" ] && [ ! -x "$app_dest/usr/bin/$APP_ID" ]; then
        die "AppImage does not contain a runnable QtEdit binary"
    fi

    # A small wrapper keeps the extracted AppDir relocatable and avoids
    # requiring FUSE at runtime.
    cat > "$STAGE_PREFIX/bin/$APP_ID" <<EOF
#!/bin/sh
APPDIR='$DATA_DIR/app'
if [ -x "\$APPDIR/AppRun" ]; then
    exec "\$APPDIR/AppRun" "\$@"
fi
export LD_LIBRARY_PATH="\$APPDIR/usr/lib:\$APPDIR/usr/lib/$(uname -m)-linux-gnu:\${LD_LIBRARY_PATH:-}"
export QT_PLUGIN_PATH="\$APPDIR/usr/plugins:\$APPDIR/usr/lib/qt6/plugins:\${QT_PLUGIN_PATH:-}"
exec "\$APPDIR/usr/bin/$APP_ID" "\$@"
EOF
    chmod 755 "$STAGE_PREFIX/bin/$APP_ID"
}

stage_desktop_integration() {
    local exec_path="$BIN_DIR/$APP_ID"

    mkdir -p "$STAGE_PREFIX/share/applications" \
             "$STAGE_PREFIX/share/icons/hicolor" \
             "$STAGE_PREFIX/share/metainfo"

    # Build a desktop file that points at the installed binary. This keeps
    # custom prefixes working even when their bin directory is not in PATH.
    local exec_escaped
    exec_escaped="$(printf '%s' "$exec_path" \
        | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/`/\\`/g' \
              -e 's/\$/\\$/g' -e 's/%/%%/g')"

    {
        while IFS= read -r line || [ -n "$line" ]; do
            case "$line" in
                Exec=*)    printf 'Exec="%s" %%F\n' "$exec_escaped" ;;
                TryExec=*) printf 'TryExec=%s\n' "$exec_path" ;;
                *)         printf '%s\n' "$line" ;;
            esac
        done < "$ROOT/$APP_ID.desktop"
    } > "$STAGE_PREFIX/share/applications/$APP_ID.desktop"

    mkdir -p "$STAGE_PREFIX/share/icons/hicolor/scalable/apps"
    install -m644 "$ROOT/resources/appicon.svg" \
        "$STAGE_PREFIX/share/icons/hicolor/scalable/apps/$APP_ID.svg"

    if [ -d "$ROOT/resources/icons/hicolor" ]; then
        cp -a "$ROOT/resources/icons/hicolor/." "$STAGE_PREFIX/share/icons/hicolor/"
    fi

    mkdir -p "$STAGE_PREFIX/share/metainfo"
    install -m644 "$ROOT/packaging/io.github.gtedit.QtEdit.metainfo.xml" \
        "$STAGE_PREFIX/share/metainfo/io.github.gtedit.QtEdit.metainfo.xml"
}

write_uninstaller() {
    mkdir -p "$STAGE_PREFIX/share/$APP_ID"

    # The path was validated above, so it can be embedded in single quotes.
    cat > "$STAGE_PREFIX/share/$APP_ID/uninstall.sh" <<EOF
#!/bin/sh
set -eu
PREFIX='$PREFIX'
MANIFEST="\$PREFIX/share/$APP_ID/install-manifest.txt"

if [ ! -f "\$MANIFEST" ]; then
    echo "$APP_NAME is not installed in \$PREFIX" >&2
    exit 1
fi

if [ ! -w "\$PREFIX" ]; then
    echo "Permission denied: \$PREFIX is not writable." >&2
    echo "Try: sudo \$0" >&2
    exit 1
fi

while IFS= read -r entry; do
    case "\$entry" in
        FILE:*) path=\${entry#FILE:} ;;
        DIR:*)  path=\${entry#DIR:} ;;
        *) continue ;;
    esac

    case "\$path" in
        "\$PREFIX"/*) ;;
        *) continue ;;
    esac

    case "\$entry" in
        DIR:*) rm -rf -- "\$path" ;;
        FILE:*) rm -f -- "\$path" ;;
    esac
done < "\$MANIFEST"

rm -f -- "\$MANIFEST"
rmdir -- "\$PREFIX/share/$APP_ID" 2>/dev/null || true

for size in 16 24 32 48 64 128 256 512; do
    rmdir -- "\$PREFIX/share/icons/hicolor/\${size}x\${size}/apps" 2>/dev/null || true
    rmdir -- "\$PREFIX/share/icons/hicolor/\${size}x\${size}" 2>/dev/null || true
done
rmdir -- "\$PREFIX/share/icons/hicolor/scalable/apps" 2>/dev/null || true
rmdir -- "\$PREFIX/share/icons/hicolor/scalable" 2>/dev/null || true
rmdir -- "\$PREFIX/share/icons/hicolor" 2>/dev/null || true
rmdir -- "\$PREFIX/share/icons" 2>/dev/null || true
rmdir -- "\$PREFIX/share/applications" 2>/dev/null || true
rmdir -- "\$PREFIX/share/metainfo" 2>/dev/null || true
rmdir -- "\$PREFIX/share" 2>/dev/null || true
rmdir -- "\$PREFIX/bin" 2>/dev/null || true

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "\$PREFIX/share/applications" >/dev/null 2>&1 || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1 \\
   && [ -f "\$PREFIX/share/icons/hicolor/index.theme" ]; then
    gtk-update-icon-cache -f -t "\$PREFIX/share/icons/hicolor" >/dev/null 2>&1 || true
fi

echo "$APP_NAME was removed from \$PREFIX."
EOF

    chmod 755 "$STAGE_PREFIX/share/$APP_ID/uninstall.sh"
    mkdir -p "$STAGE_PREFIX/bin"
    install -m755 "$STAGE_PREFIX/share/$APP_ID/uninstall.sh" \
        "$STAGE_PREFIX/bin/$APP_ID-uninstall"
}

write_manifest() {
    local manifest="$STAGE_PREFIX/share/$APP_ID/install-manifest.txt"
    local file

    {
        echo "FILE:$BIN_DIR/$APP_ID"
        echo "FILE:$BIN_DIR/$APP_ID-uninstall"
        echo "FILE:$DESKTOP_FILE"
        echo "FILE:$ICON_DIR/scalable/apps/$APP_ID.svg"
        for size in 16 24 32 48 64 128 256 512; do
            echo "FILE:$ICON_DIR/${size}x${size}/apps/$APP_ID.png"
        done
        echo "FILE:$METAINFO_FILE"
        echo "FILE:$DATA_DIR/uninstall.sh"
        if [ -d "$STAGE_PREFIX/share/$APP_ID/app" ]; then
            echo "DIR:$DATA_DIR/app"
        fi
    } > "$manifest"
}

# --------------------------------------------------------------- install

install_files() {
    log "Installing to $PREFIX"
    priv mkdir -p "$PREFIX"

    # Remove an old AppImage tree so stale bundled libraries are never left
    # behind when switching install methods.
    priv rm -rf "$DATA_DIR/app" 2>/dev/null || true

    # Copy the small integration files, but move the (potentially large)
    # extracted AppImage separately to avoid duplicating it in the staging
    # directory.
    local staged_app=""
    if [ -d "$STAGE_PREFIX/share/$APP_ID/app" ]; then
        staged_app="$STAGE/staged-app"
        mv "$STAGE_PREFIX/share/$APP_ID/app" "$staged_app"
    fi

    if ! priv cp -a "$STAGE_PREFIX/." "$PREFIX/"; then
        die "failed to copy files to $PREFIX (free some space or set QTEDIT_TMPDIR to a larger filesystem)"
    fi

    if [ -n "$staged_app" ]; then
        priv mkdir -p "$DATA_DIR"
        if ! priv mv "$staged_app" "$DATA_DIR/app"; then
            die "failed to move the AppImage payload into $DATA_DIR/app"
        fi
    fi

    if [ "$MODE" = "system" ] && [ "$(id -u)" -ne 0 ]; then
        priv chown root:root "$BIN_DIR/$APP_ID" "$BIN_DIR/$APP_ID-uninstall" \
             "$DESKTOP_FILE" "$METAINFO_FILE" 2>/dev/null || true
        priv find "$ICON_DIR" -type f -name "$APP_ID.*" -exec chown root:root {} + 2>/dev/null || true
        priv chown -R root:root "$DATA_DIR" 2>/dev/null || true
    fi
}

refresh_caches() {
    if command -v update-desktop-database >/dev/null 2>&1; then
        priv update-desktop-database "$APPS_DIR" >/dev/null 2>&1 || true
    fi
    if command -v gtk-update-icon-cache >/dev/null 2>&1 \
       && [ -f "$ICON_DIR/index.theme" ]; then
        priv gtk-update-icon-cache -f -t "$ICON_DIR" >/dev/null 2>&1 || true
    fi
}

print_summary() {
    printf '\n'
    log "$APP_NAME $VERSION installed"
    printf '  Launcher:   %s\n' "$BIN_DIR/$APP_ID"
    printf '  Menu entry: %s\n' "$DESKTOP_FILE"
    printf '  Uninstall:  %s\n' "$BIN_DIR/$APP_ID-uninstall"

    case ":$PATH:" in
        *":$BIN_DIR:"*) ;;
        *) warn "$BIN_DIR is not in your PATH; add it to launch QtEdit by name." ;;
    esac

    if [ "$MODE" = "system" ]; then
        if [ "$(id -u)" -eq 0 ]; then
            printf '\nUninstall with: %s --uninstall\n' "$BIN_DIR/$APP_ID-uninstall"
        else
            printf '\nUninstall with: sudo %s --uninstall\n' "$BIN_DIR/$APP_ID-uninstall"
        fi
    else
        printf '\nRun with: %s\n' "$BIN_DIR/$APP_ID"
    fi
}

# --------------------------------------------------------------- main

if [ -n "$APPIMAGE" ]; then
    case "$APPIMAGE" in
        /*) ;;
        *) APPIMAGE="$ROOT/$APPIMAGE" ;;
    esac
    log "Installing from AppImage: $APPIMAGE"
    stage_appimage_install "$APPIMAGE"
else
    build_application
    log "Staging $(basename "$BINARY")"
    stage_source_install "$BINARY"
fi

stage_desktop_integration
write_uninstaller
write_manifest
install_files
refresh_caches
print_summary
