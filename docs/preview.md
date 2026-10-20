# WebUI, JPEG and browser preview

The interface uses OpenIPC templates/assets adapted to the actual Anyka backend. The public C gateway exposes an explicit route allowlist, while haserl runs behind a private loopback server. Only implemented camera functions should appear as working controls. Majestic configuration, console and flash CGI must remain inaccessible.

JPEG accepts an authenticated web session or HTTP Digest. Preview uses the SDK private substream, a bounded RTSP parser and C fMP4 muxer. Video is H.264 without transcoding; browser audio is lossless FLAC derived from PCMA. The muxer adapts MIT-licensed go2rtc 1.9.14 concepts and retains attribution. It does not execute Go code.

One browser preview reader is allowed. Memory guards check about 4608 KiB before start and 4096 KiB while running. Preview does not consume one of the two public native RTSP peers, but its encoding/memory workload still affects the same camera.

`ANYKA_WEB_UI=0` selects the headless behavior. The full profile includes working Information/Preview/Camera/Stream URLs/Logs/Diagnostics/Extensions/Access views and only supported backend routes. Static files for upstream features are included for provenance; their presence does not imply the corresponding admin endpoints are exposed.

CGI shebangs on root-owned FAT files must match `/media/usr/bin/haserl`. The `p/openipc-sh` helper provides musl BusyBox shell semantics instead of the stock shell. Use the original view layout because page scripts resolve paths relative to it.

Do not expose the private CGI or SDK listener externally. Do not enable arbitrary go2rtc administration APIs in the legacy fallback. On the older Go preview path only the static reader and `/api/ws` were permitted; administration paths returned 404.

---

[Documentation index](README.md) · [Project overview](../README.md)
