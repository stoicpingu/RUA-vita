#!/bin/sh
set -eu
rua_project=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
: "${VITASDK:?Set VITASDK to your native VitaSDK installation}"
mkdir -p "$rua_project/.scratch" "$rua_project/.cache"
export TMPDIR="$rua_project/.scratch"
export TMP="$TMPDIR"
export TEMP="$TMPDIR"
export XDG_CACHE_HOME="$rua_project/.cache"
export CCACHE_DIR="$rua_project/.cache/ccache"
cmake -S "$rua_project" -B "$rua_project/build" -DCMAKE_BUILD_TYPE=Release
# GNU Make 4.4 defaults to FIFO jobservers, which ExFAT cannot create.
if command -v gmake >/dev/null 2>&1 && gmake --help | grep -q -- '--jobserver-style'; then
  export MAKEFLAGS="${MAKEFLAGS:-} --jobserver-style=pipe"
fi
cmake --build "$rua_project/build" --parallel 4
