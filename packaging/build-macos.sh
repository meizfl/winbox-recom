#!/bin/bash
set -euo pipefail
src=$(cd "$(dirname "$0")/.." && pwd)
out=${1:-"$src/build/macos-release"}
mkdir -p "$out"
out=$(cd "$out" && pwd)
prefix="$out/deps"
arch=${WINBOX_MACOS_ARCH:-$(uname -m)}
case "$arch" in
  x86_64) minimum=10.14 ;;
  arm64) minimum=11.0 ;;
  *) echo "Unsupported macOS architecture: $arch" >&2; exit 1 ;;
esac
export MACOSX_DEPLOYMENT_TARGET="$minimum"
export SDKROOT=$(xcrun --sdk macosx --show-sdk-path)
export CFLAGS="-O2 -arch $arch -mmacosx-version-min=$minimum"
export CXXFLAGS="$CFLAGS"
export LDFLAGS="-arch $arch -mmacosx-version-min=$minimum"
export PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig:$prefix/share/pkgconfig"
unset PKG_CONFIG_PATH || true
export CMAKE_PREFIX_PATH="$prefix"
if [ ! -f "$prefix/.winbox-sdk-ready" ]; then
python3 "$src/packaging/fetch_sources.py" zlib png expat freetype fontconfig pixman cairo sdl --cache "$out/cache" --extract "$out/sources"
cmake_dep() {
  local name=$1; shift
  cmake -S "$out/sources/$name" -B "$out/$name-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$minimum" -DCMAKE_OSX_ARCHITECTURES="$arch" \
    -DBUILD_SHARED_LIBS=OFF "$@"
  cmake --build "$out/$name-build" -j4
  cmake --install "$out/$name-build"
}
meson_dep() {
  local name=$1; shift
  meson setup "$out/$name-build" "$out/sources/$name" --prefix="$prefix" --libdir=lib --default-library=static --buildtype=release "$@"
  meson compile -C "$out/$name-build" -j4
  meson install -C "$out/$name-build"
}
cmake_dep zlib -DZLIB_BUILD_TESTING=OFF
cmake_dep png -DPNG_SHARED=OFF -DPNG_TESTS=OFF -DPNG_TOOLS=OFF
cmake_dep expat -DEXPAT_SHARED_LIBS=OFF -DEXPAT_BUILD_TESTS=OFF -DEXPAT_BUILD_TOOLS=OFF -DEXPAT_BUILD_EXAMPLES=OFF -DEXPAT_BUILD_DOCS=OFF
cmake_dep freetype -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_BROTLI=ON
meson_dep fontconfig -Ddoc=disabled -Dnls=disabled -Dtests=disabled -Dtools=disabled -Dcache-build=disabled -Dxml-backend=expat
meson_dep pixman -Dtests=disabled -Ddemos=disabled -Dgtk=disabled -Dlibpng=disabled
meson_dep cairo -Dfontconfig=enabled -Dfreetype=enabled -Dpng=enabled -Dquartz=disabled \
  -Dxlib=disabled -Dxcb=disabled -Dxlib-xcb=disabled -Dglib=disabled -Dlzo=disabled \
  -Dspectre=disabled -Dsymbol-lookup=disabled -Dtests=disabled -Dgtk_doc=false -Dgtk2-utils=disabled
cmake_dep sdl -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_DEPS_SHARED=OFF \
  -DSDL_AUDIO=OFF -DSDL_JOYSTICK=OFF -DSDL_HAPTIC=OFF -DSDL_SENSOR=OFF -DSDL_CAMERA=OFF \
  -DSDL_HIDAPI=OFF -DSDL_HIDAPI_LIBUSB=OFF -DSDL_TRAY=OFF -DSDL_TEST_LIBRARY=OFF
touch "$prefix/.winbox-sdk-ready"
fi
cmake -S "$src" -B "$out/app-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DPython3_EXECUTABLE="$(command -v python3)" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="$minimum" -DCMAKE_OSX_ARCHITECTURES="$arch" \
  -DWINBOX_STATIC_DEPENDENCIES=ON -DIconv_INCLUDE_DIR="$SDKROOT/usr/include" \
  -DIconv_LIBRARY="$SDKROOT/usr/lib/libiconv.tbd"
cmake --build "$out/app-build" -j4
ctest --test-dir "$out/app-build" --output-on-failure
bundle="$out/dist/WinBox Recom.app"
mkdir -p "$bundle/Contents/MacOS" "$bundle/Contents/Resources"
cp "$out/app-build/winbox3" "$bundle/Contents/MacOS/winbox3"
python3 - "$src/packaging/Info.plist" "$bundle/Contents/Info.plist" "$minimum" <<'PYPLIST'
import plistlib,sys
with open(sys.argv[1], 'rb') as file: info=plistlib.load(file)
info['LSMinimumSystemVersion']=sys.argv[3]
with open(sys.argv[2], 'wb') as file: plistlib.dump(info,file)
PYPLIST
cp "$src/packaging/fonts.conf" "$bundle/Contents/Resources/fonts.conf"
python3 "$src/packaging/extract_icon.py" "$src/winbox64.v3.43p10.exe" "$out/WinBox.ico"
mkdir -p "$out/WinBox.iconset"
for size in 16 32 128 256 512; do
  sips -s format png -z "$size" "$size" "$out/WinBox.ico" --out "$out/WinBox.iconset/icon_${size}x${size}.png" >/dev/null
done
iconutil -c icns "$out/WinBox.iconset" -o "$bundle/Contents/Resources/WinBox.icns"
strip -x "$bundle/Contents/MacOS/winbox3"
python3 "$src/packaging/verify_macos.py" "$bundle/Contents/MacOS/winbox3" "$arch" "$minimum"
codesign --force --deep --sign - "$bundle"
ditto -c -k --sequesterRsrc --keepParent "$bundle" "$out/dist/winbox3-macos-$arch.zip"
(cd "$out/dist"; shasum -a 256 winbox3-macos-$arch.zip > SHA256SUMS)
