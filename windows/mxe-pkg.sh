#!/usr/bin/env bash
VERSION=$1
ARCH=$2
BUILD=_win32
PKG_DIR=$BUILD/cbird-win
ZIP=cbird-windows-$VERSION-$ARCH.zip
MXE_BIN="$MXE_DIR/usr/$MXE_TARGET/bin"
OPENCV_BIN="$CV_BUILD/install/x64/mingw/bin"
QT_DIR="$MXE_DIR/usr/$MXE_TARGET/qt6"
QT_BIN="$QT_DIR/bin"
STRIP="$MXE_DIR/usr/bin/$MXE_TARGET-strip"

CROSS_BIN="$MXE_BIN"
if [[ ! -d "$EXTRA_PREFIX/bin" ]]; then
    CROSS_BIN="$EXTRA_PREFIX/bin";
fi

echo building $VERSION $ARCH in $PKG_DIR

echo "programs..."
mkdir -p "$PKG_DIR"
cp -au cbird.exe "$PKG_DIR/"

# copy qt plugins we actually need
echo "qt plugins.."
mkdir -p "$PKG_DIR/plugins"
cp -auv "$QT_DIR/plugins/iconengines" "$PKG_DIR/plugins/"
cp -auv "$QT_DIR/plugins/imageformats" "$PKG_DIR/plugins/"
cp -auv "$QT_DIR/plugins/generic" "$PKG_DIR/plugins/"
cp -auv "$QT_DIR/plugins/platforms" "$PKG_DIR/plugins/"
cp -auv "$QT_DIR/plugins/sqldrivers" "$PKG_DIR/plugins/"
cp -auv "$QT_DIR/plugins/styles" "$PKG_DIR/plugins/"

#echo "termcap..."
#TERMCAP_DIR="$MXE_BIN/../share/terminfo"
#mkdir -p "$PKG_DIR/termcap"
#for cap in "$TERMCAP_DIR/m/ms-"* "$TERMCAP_DIR/c/cyg"*; do
#  cp -auv "$cap" "$PKG_DIR/termcap/"
#done

for exe in sqlite3.exe; do
    cp -au "$MXE_DIR/usr/$MXE_TARGET/bin/$exe" "$PKG_DIR/"
done

for exe in ffplay.exe ffprobe.exe ffmpeg.exe; do
    cp -auv "$CROSS_BIN/$exe" "$PKG_DIR/"
done

# use objdump to recursively find all DLL dependencies
# (replaces the old wine-based approach which required wine to be installed)
OBJDUMP="$MXE_DIR/usr/bin/$MXE_TARGET-objdump"
DLL_DIRS=("$CROSS_BIN" "$OPENCV_BIN" "$QT_BIN" "$MXE_BIN")

declare -A SEEN_DLLS
collect_deps() {
    local file="$1"
    local base
    base=$(basename "$file" | tr '[:upper:]' '[:lower:]')
    [[ ${SEEN_DLLS[$base]+x} ]] && return
    SEEN_DLLS[$base]=1

    local deps
    deps=$("$OBJDUMP" -p "$file" 2>/dev/null | awk '/DLL Name:/{print $3}')
    for dll in $deps; do
        local dlower
        dlower=$(echo "$dll" | tr '[:upper:]' '[:lower:]')
        [[ ${SEEN_DLLS[$dlower]+x} ]] && continue

        local found=""
        for dir in "${DLL_DIRS[@]}"; do
            if [ -f "$dir/$dll" ]; then
                found="$dir/$dll"
                break
            fi
        done

        if [ -n "$found" ]; then
            cp -auv "$found" "$PKG_DIR/"
            collect_deps "$found"
        fi
    done
}

echo "collecting dlls..."
for file in "$PKG_DIR"/*.exe; do
    [ -f "$file" ] && collect_deps "$file"
done
for file in $(find "$PKG_DIR/plugins" -name '*.dll' 2>/dev/null); do
    collect_deps "$file"
done

echo "strip binaries..."
$STRIP $PKG_DIR/*.exe $PKG_DIR/*.dll

rm -fv "$ZIP"
#(cd "$BUILD" && zip -r ../"$ZIP" cbird)
