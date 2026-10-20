#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
runtime_dir="$script_dir/../sd-openipc-test/openipc-persist/sdk-rtsp"

for stream in vs0 vs1; do
    ffmpeg -hide_banner -loglevel error -loop 1 -framerate 20 \
        -i "$script_dir/privacy-slate-$stream.png" -frames:v 1 -an \
        -c:v libx264 -profile:v baseline -level:v 4.2 -pix_fmt yuv420p \
        -x264-params keyint=1:min-keyint=1:scenecut=0:repeat-headers=1 \
        -f h264 -y "$runtime_dir/privacy-slate-$stream.h264"
done

ffmpeg -hide_banner -loglevel error -i "$script_dir/privacy-slate-vs0.png" \
    -frames:v 1 -q:v 4 -y "$runtime_dir/privacy-slate.jpg"
