#!/bin/sh
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPOSITORY_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
TOOLCHAIN_DIR="$REPOSITORY_DIR/openipc-ak3918-build/output-toolchain-ak3918ev300/host/bin"
SDK="$SCRIPT_DIR/../AnyCloud39AV130_SDK_V1.03/platform"
OUTPUT="$SCRIPT_DIR/../sd-openipc-test/openipc-persist/sdk-rtsp/anyka-native-rtsp.so"
"$TOOLCHAIN_DIR/arm-linux-gcc" -shared -fPIC -Os -Wall -Wextra -Werror \
    -march=armv5te -fno-stack-protector -nostdlib -Wl,--hash-style=sysv \
    -I"$SDK/sample/rtsp" -isystem "$SDK/include" -o "$OUTPUT" "$SCRIPT_DIR/anyka-native-rtsp.c" -lgcc
"$TOOLCHAIN_DIR/arm-linux-readelf" -h -d "$OUTPUT"
