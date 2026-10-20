# Memory and storage budget

64 MiB installed RAM is not 64 MiB available to Linux. The DT reserves 0x02000000 bytes at 0x81400000 for video. Linux reports about 26860 KiB total; kernel and other reservations explain why subtracting only the video carve-out is insufficient.

| Resource | Current limit / contract |
| --- | --- |
| Vendor main/sub/audio circular buffers | 2 MiB / 512 KiB / 256 KiB |
| App video queue | frame_depth 2, reduced from vendor request 200 |
| Native RTSP readers | 2 total, TCP only |
| Encoded frame per native reader | 512 KiB maximum |
| Browser preview readers | 1; separate private SDK route |
| Preview encoded frame | 512 KiB; bounded RTP/metadata buffers |
| HTTP clients | 6 according to gateway build profile |
| Web sessions / Digest nonces | 8 / 32 |
| HTTP header budget | 8192 bytes |
| Speaker chunk | 1280 bytes maximum, acknowledged |
| Speaker reported queue | 8192 bytes guard |
| Person inference copy | 86400 bytes, reusable 320×180 DMA frame |

Preserve the SDK circular buffers. Queue depth and circular-buffer sizes address different problems; do not replace `cirbuf_new`. Slow TCP peers are disconnected rather than buffered without bound.

Check `MemAvailable`, SDK RSS, gateway RSS, tmpfs consumption and log growth under each workload. Filesystem free space and `MemFree` alone are misleading. Source-only repository size does not predict compressed firmware size; packing libraries, templates and runtime dependencies must be measured with the actual image builder.

Observed post-install available RAM was about 9.3–9.5 MiB in several recent short checks. Older native media/audio/NPU measurements had less headroom. These figures are different workloads, not a guaranteed minimum.

---

[Documentation index](README.md) · [Project overview](../README.md)
