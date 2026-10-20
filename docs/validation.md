# Observed results and evidence limits

These are curated summaries of existing laboratory reports, not new tests performed during documentation preparation. Raw local reports contain private camera/host information and are not distributed. The selected observations are machine-readable in `manifests/observations.json`; evidence file hashes establish provenance, not independent verification.

| Date / stage | Observation | Limits |
| --- | --- | --- |
| 11 Oct native RTSP | 922 decoded frames / about 45 s, ~18.8% CPU, 6.3–6.8 MiB available; HA HLS startup 7.92 s | Earlier SD/native stage, not latest internal release |
| 11 Oct NPU | 595 HD frames / 30 s, 33 inferences, no SDK error | No demonstrated real-human recognition |
| Native audio | 1125 silent PCM frames / 45 s, AO queue peak 3648 bytes, HD concurrent in separate runs | Silence does not establish acoustic quality |
| 17 Oct first internal deployment | 901 HD frames / 45.23 s; JPEG 200, 22684 bytes / 0.532 s; preview 898 frames / 45.16 s | Reboot from internal flash; physical SD-free power-on not witnessed |
| 19 Oct ABI/Digest repair | 1799 HD frames / 90.97 s, no decode errors; 25 PCM frames, EOF ack, SDK survived | Short protocol/media run |
| 19 Oct PTZ correction | 201 HD frames / 10 s, centered 2017/482; user direction confirmation | One mechanical assembly |
| 20 Oct pre-update | 51333.12 s uptime (~14 h 15 m), same SDK/gateway processes, 9640 KiB available, bounded sampled logs | Process uptime is not continuous decoded-video evidence |
| 20 Oct recovery RAM | Controlled SDK exit recovered in 25 s, unchanged gateway/PTZ; 891 HD frames / 45 s | One controlled failure mode |
| 20 Oct reliability installed | 901 HD frames / 45.99 s, 12 OSD ticks, update 1–6 ms; 9548 KiB available; config RO | Final feature set not overnight validated |
| 20 Oct presets installed | Homing 2017/482 then saved startup 1985/482; UTF-8 persisted; Stop interrupted a long recall; 901 HD frames / 45 s | Counted positions, no absolute encoder validation |
| 20 Oct HA 0.3.0 | Preset/status/card routes and invalid slot behavior checked; 201 frames / 10 s | Custom integration, not HA core acceptance |

Recent preset checks left startup=0, no test slots and position restored to 2017/482. The final configuration mount was read-only with about 308 KiB free and 9704 KiB RAM available in that sample.

Unverified: final-version overnight stability, physical power loss at each persistence/flash stage, SD physically absent cold boot, multiple viewers sustained with simultaneous talkback/NPU, real person detection, automatic tracking safety across full mechanical travel, independent security review, and a clean upstream kernel/Buildroot boot.

---

[Documentation index](README.md) · [Project overview](../README.md)
