#!/bin/sh
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -z "${WINBOX_PROFILE:-}" ]; then
    export WINBOX_PROFILE="$project_dir/build/full/profile"
fi
exec "$project_dir/build/full/winbox3" "$@"
