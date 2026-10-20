#!/bin/sh
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPOSITORY_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
TOOLCHAIN="$REPOSITORY_DIR/openipc-ak3918-build/output-toolchain-ak3918ev300/host/bin"
OUTPUT="$SCRIPT_DIR/../sd-openipc-test/openipc-persist/sdk-rtsp/anyka-ptz"
"$TOOLCHAIN/arm-linux-gcc" -std=c99 -D_DEFAULT_SOURCE -Os -Wall -Wextra -Werror -march=armv5te -s -o "$OUTPUT" "$SCRIPT_DIR/anyka-ptz.c"
"$TOOLCHAIN/arm-linux-readelf" -h "$OUTPUT"