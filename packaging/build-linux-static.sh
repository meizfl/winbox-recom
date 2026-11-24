#!/bin/sh
set -eu
# Run in packaging/linux-static.Dockerfile. Output and cache must be writable.
src=${1:-/src}
out=${2:-/out}
prefix="$out/deps"
mkdir -p "$out" "$prefix"
python3 "$src/packaging/fetch_sources.py" xau xrandr dbus sdl --cache "$out/cache" --extract "$out/sources"
export PKG_CONFIG_PATH="$prefix/lib/pkgconfig:$prefix/share/pkgconfig"
export CMAKE_PREFIX_PATH="$prefix"
export CFLAGS="-O2 -fPIC"
for name in xau xrandr; do
  (cd "$out/sources/$name"; ./configure --prefix="$prefix" --disable-shared --enable-static; make -j4; make install)
done
meson setup --reconfigure "$out/dbus-build" "$out/sources/dbus" --prefix="$prefix" --libdir=lib --default-library=static --buildtype=release \
  -Dapparmor=disabled -Dselinux=disabled -Dlibaudit=disabled -Dsystemd=disabled \
  -Dlaunchd=disabled -Dx11_autolaunch=disabled -Dmodular_tests=disabled \
  -Ddoxygen_docs=disabled -Dducktype_docs=disabled -Dxml_docs=disabled -Dqt_help=disabled \
  -Dmessage_bus=false -Dtools=false
meson compile -C "$out/dbus-build" -j4
meson install -C "$out/dbus-build"
python3 "$src/packaging/static_sdl_dbus.py" "$out/sources/sdl"
cmake -S "$out/sources/sdl" -B "$out/sdl-build" -G Ninja -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_INSTALL_LIBDIR=lib \
  -DCMAKE_BUILD_TYPE=Release -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_DEPS_SHARED=OFF \
  -DSDL_X11_SHARED=OFF -DSDL_WAYLAND_SHARED=OFF -DSDL_WAYLAND_LIBDECOR=OFF \
  -DSDL_AUDIO=OFF -DSDL_JOYSTICK=OFF -DSDL_HAPTIC=OFF -DSDL_SENSOR=OFF \
  -DSDL_CAMERA=OFF -DSDL_POWER=OFF -DSDL_TRAY=OFF -DSDL_HIDAPI=OFF -DSDL_LIBUDEV=OFF \
  -DSDL_KMSDRM=OFF -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF -DSDL_VULKAN=OFF -DSDL_RENDER=OFF -DSDL_GPU=OFF
cmake --build "$out/sdl-build" -j4
cmake --install "$out/sdl-build"
cmake -S "$src" -B "$out/app-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DWINBOX_STATIC_DEPENDENCIES=ON -DWINBOX_FULLY_STATIC=ON
cmake --build "$out/app-build" -j4
mkdir -p "$out/dist"
cp "$out/app-build/winbox3" "$out/dist/winbox3-linux-amd64"
strip "$out/dist/winbox3-linux-amd64"
if readelf -l "$out/dist/winbox3-linux-amd64" | grep -q INTERP; then exit 1; fi
if readelf -d "$out/dist/winbox3-linux-amd64" | grep -q NEEDED; then exit 1; fi
sha256sum "$out/dist/winbox3-linux-amd64" > "$out/dist/SHA256SUMS"
