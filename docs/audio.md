# Microphone, speaker and native audio protocols

Microphone RTP uses PCMA. Vendor AAC was disabled because it emitted an invalid RTP track. Public audio shares the RTSP video timeline and starts only after the first complete IDR; up to 64 historical SDK audio callbacks are drained per poll before initial delivery.

Speaker output lives inside the SDK process using official `ak_ao_open/send_frame/flush/cancel/close`. The gateway authenticates WebSocket sessions, enforces bounded framing and talks to `/conf/anyka-private/native-audio.sock`. No password or HA1 reaches the SDK.

| Protocol | Payload / contract |
| --- | --- |
| AO01 | Legacy accepted PCM control |
| AO02 | Mono 8 kHz S16LE PCM, optional native speaker noise reduction |
| MP02 | Original-source MP3 metadata and bounded compressed chunks; SDK ADEC + VQE to mono 8 kHz |

Chunks are at most 1280 bytes and acknowledged. DAC pacing is real-time. Sessions are limited to five minutes; MP3 additionally has a 20 MiB input cap. EOF waits for a bounded DAC drain and returns an acknowledgement. A separate cancellation supervisor handles Stop/disconnect/revocation.

Keep GPIO67 active-low amplifier semantics and mute on every exit. Driver ioctl success can be positive. Retain the original AEC 512-sample parameters. Query the AO buffer once per 5120 PCM bytes: querying every frame caused SDK diagnostics and approximately 85% CPU. Bound reported queued bytes at 8192.

16 kHz AO conflicted with SDK AI/AEC and is not offered. MP3 source tests covered mono/stereo 8–48 kHz; this does not imply 48 kHz speaker output. The final output remains mono 8 kHz.

Silent protocol tests verify pacing, drain, memory and process survival, not intelligibility, loudness, echo cancellation or overnight acoustic quality. Retain original uClibc libgcc_s for SDK thread cancellation; see the ABI chapter.

---

[Documentation index](README.md) · [Project overview](../README.md)
