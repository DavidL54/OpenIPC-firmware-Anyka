#!/bin/sh
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUTPUT_DIR=$SCRIPT_DIR/../sd-openipc-test/openipc-persist/sdk-rtsp/www/a
test -f "$OUTPUT_DIR/wav-encoder.LICENSE"
WORK_DIR=$(mktemp -d)
trap 'rm -rf "$WORK_DIR"' 0 HUP INT TERM
npm install --prefix "$WORK_DIR" --no-audit --no-fund wav-encoder@1.3.0 esbuild@0.25.0
"$WORK_DIR/node_modules/.bin/esbuild" "$WORK_DIR/node_modules/wav-encoder/index.js" \
	--bundle --platform=browser --format=esm --minify --outfile="$OUTPUT_DIR/wav-encoder.js"