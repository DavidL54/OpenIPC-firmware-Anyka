# RTSP and encoded video

| Route | Codec | Size | Nominal frame rate | Nominal configured bitrate |
| --- | --- | --- | --- | --- |
| /vs0 | H.264 | 2304×1296 | 20 fps | 2048 kbit/s |
| /vs1 | H.264 | 640×360 | 20 fps | 400 kbit/s |
| Audio | PCMA / G.711 A-law | mono 8 kHz | sample-based timestamps | codec dependent |

Public :554 accepts Digest with qop authentication, TCP interleaving, read-only RTSP routes and at most two readers. There is no anonymous/Basic fallback, including localhost. Diagnostic readers consume the same two slots as Home Assistant.

The native server uses official SDK callbacks and a per-frame maximum of 512 KiB. Blocked readers are disconnected. Preserve the vendor circular-buffer sizes and the separate two-frame app queue cap. Queue caps can drop frames under overload and must be measured when changed.

The sample opens encoders through `ak_venc_open_ex`. Keep the native one-second H.264 GOP cap (20 frames at 20 fps) without accidentally changing bitrate, rate control or JPEG quality. A complete IDR gates initial audio. PLAY must supply correct RTP-Info and share the video/audio timestamp origin. RTCP uses a running shared clock, not current NTP paired with a stale last-packet timestamp.

A previous FFmpeg failure came from rejecting a valid Digest cnonce change. Correct replay protection tracks nonce/count semantics without treating every new cnonce as an attack. The repair and retained native GOP reduce HA startup problems; they do not justify removing authentication.

The vendor private RTSP sample's own username/password controls are ineffective. Keep :8555 bound to loopback by the mandatory interposer, bring lo up first, and verify the expected binary hash in a complete runtime. Never expose the vendor listener as a shortcut.

---

[Documentation index](README.md) · [Project overview](../README.md)
