#!/bin/sh
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -z "${WINBOX_PROFILE:-}" ]; then
    export WINBOX_PROFILE="$project_dir/build/full/profile"
fi
binary=${WINBOX_BINARY:-"$project_dir/build/full/winbox3"}
if [ -z "${WINBOX_BINARY:-}" ] && [ "$(uname -s)" = Linux ]; then
    if [ -x "$project_dir/build/linux-static/dist/winbox3-linux-amd64" ]; then
        binary="$project_dir/build/linux-static/dist/winbox3-linux-amd64"
    elif [ -x "$project_dir/build/linux-check/winbox3" ]; then
        binary="$project_dir/build/linux-check/winbox3"
    fi
fi
exec "$binary" "$@"
