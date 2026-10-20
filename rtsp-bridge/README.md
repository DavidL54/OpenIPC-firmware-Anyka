# Native SDK extensions and legacy bridge source

Production: anyka-native-rtsp.c and audio headers, anyka-rtsp-auth.c, anyka-daynight.c and control headers, anyka-ptz.c, anyka-gpio.c, bounded logging and AAC disabling.

Read the [source map](../docs/source-map.md), [RTSP](../docs/rtsp.md), [audio](../docs/audio.md), [GPIO](../docs/gpio.md), [PTZ](../docs/ptz.md) and [build](../docs/build.md) chapters. Exact SDK headers/libraries are external inputs.

Legacy Go and diagnostic fixtures are retained. frame-probe / guessed gv_plat_api calls are unsafe for the encoder and must not be enabled. mdq-tap is diagnostic only. No included fixture has been rerun as part of this documentation export.

Current preload integration is not compliant with the reviewed OpenIPC upstream delivery rules. See the integration roadmap before proposing these files as an upstream package.
