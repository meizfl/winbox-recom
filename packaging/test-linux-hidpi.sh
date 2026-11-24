#!/bin/sh
set -eu
out=${1:-/out}
export XDG_RUNTIME_DIR=$(mktemp -d /tmp/winbox-hidpi-XXXXXX)
chmod 700 "$XDG_RUNTIME_DIR"
export WAYLAND_DISPLAY=winbox-hidpi
export SDL_VIDEO_DRIVER=wayland
export WINBOX_EXPECT_DENSITY=2
unset DISPLAY WAYLAND_DEBUG
weston --backend=headless-backend.so --renderer=pixman --width=3000 --height=2000 --scale=2 --socket="$WAYLAND_DISPLAY" --idle-time=0 --log="$out/weston-hidpi.log" &
compositor=$!
trap 'kill "$compositor" 2>/dev/null || true; rm -rf "$XDG_RUNTIME_DIR"' EXIT INT TERM
for attempt in $(seq 1 50); do
  [ -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ] && break
  kill -0 "$compositor"
  sleep 0.1
done
test -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY"
"$out/app-build/native-surface-tests"
ctest --test-dir "$out/app-build" -R '^native-hidpi-smoke$' --output-on-failure
