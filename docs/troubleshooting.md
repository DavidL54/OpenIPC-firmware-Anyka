# Troubleshooting by symptom

| Symptom | First checks / known finding |
| --- | --- |
| Audio works but HA video fails | Close extra RTSP readers; inspect Digest cnonce/count handling, one-second IDR GOP and FFmpeg timestamps |
| Vendor RTSP connects anonymously | Public listener isolation is broken; do not expose :8555; restore mandatory auth interposer |
| SDK CPU near 100% after startup | Check lo and private :8555 listener before changing encoder settings |
| `/tmp` unexpectedly full | Check repeated `pMgr->iListenFd=0` vendor log; filter only that specific noisy format |
| SDK exits after speaker close | Verify original uClibc libgcc_s is present and loaded; cancellation uses it dynamically |
| Clock advances every four seconds | Verify independent OSD worker rather than day/night loop; compare rendered frames with status |
| PTZ up/down reversed | Verify board orientation and application mapping, not a generic reversal of axis ranges |
| Saved position recall freezes UI | Check independent Stop path, 32-step chunks, 60/65/70 s helper/gateway/HA deadlines |
| Presets vanish or cannot save | Check private JSON validity, free JFFS2, persistence lock and RO remount; never clear credentials to repair presets |
| NPU ioctl rejected | Stock 2.0.08 differs from required SDK 2.0.25; use verified RAM load and fail-safe restore |
| ARM illegal instruction | Inspect ARM attributes; generic ARMv7 releases are unsuitable |
| Library loads but crashes | Check uClibc/musl boundary and exact SDK ABI, not only pathname |
| SD version works, compressed image times out | Measure SquashFS/cache, CPU and memory with same workload; old Go path was investigated separately |
| Fresh source checkout cannot build full images | Private stock inputs/vendor runtime intentionally excluded; read dependency inventory |

Start with read-only observations. Avoid repeatedly restarting SDK, loading guessed modules, disabling authentication or rehoming motors to hide a resource problem. Preserve the first error and resource state; later log noise can obscure the cause.

The stock proprietary RTSP service and incompatible EV300 kernel are not fallbacks. Diagnostic sources that touch guessed `gv_plat_api()` indices or frame probes are excluded from the production path because they corrupted encoder state.

---

[Documentation index](README.md) · [Project overview](../README.md)
