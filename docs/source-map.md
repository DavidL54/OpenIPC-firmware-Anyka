# Source map and maintenance boundaries

| Directory | Contents / role |
| --- | --- |
| native-gateway/ | Production C HTTP/auth, preview mux, audio transport, presets, vendored open-source crypto/JSON |
| rtsp-bridge/ | Native SDK RTSP/audio, auth isolation, GPIO/PTZ helpers, SDK controls, OSD, detection; legacy Go and diagnostic sources |
| internal-firmware/ | Internal rootfs/app builders, ELF closure, BusyBox configuration, startup and monitor, private-config staging logic |
| sd-openipc-test/ | Filtered SD/RAM runtime scripts and WebUI source/assets, no boot/runtime binary images |
| homeassistant/ | Custom integration 0.3.0 and matching controls card |
| patches/ | Historical userspace compatibility patch |
| reference/sd-workflow/ | Historical SD preparation and boot tools |
| reference/lab-ssh-maintenance/ | Fixed-board SSH/RAM transition research, not reusable deployment |
| hardware/ | Machine-readable board and partition facts |
| manifests/ | Export provenance, checksums, excluded runtime inventory and observations |
| docs/ | Maintainer and user documentation |
| tools/ | Local source publication checks, no deployment action |
| upstream/ | Integration issue/PR drafts and target mapping |

The current C source is the reference implementation. `homeassistant/mjpeg_normalizer.py` is an optional historical authenticated JPEG-to-multipart helper, not the current native RTSP/HA media path. Go gateway files remain because they explain the preceding design and carry useful fixtures. They are not a requirement for the current production path.

`frame-probe.c` and guessed `gv_plat_api()` experiments must not be enabled: they corrupted encoder state. `mdq-tap` is diagnostic only. Stock proprietary RTSP exposed no usable media track and is not a fallback. The EV300 boot images tried early in development are not included.

The source-export manifest records originals and exported SHA-256 values. Personal LAN examples, embedded laboratory address defaults and related synthetic fixture expectations are replaced with RFC 5737 addresses. Configure the real setup-computer address explicitly before a private deployment. The standalone C gateway build gains environment overrides for tool selection. Core camera logic is otherwise copied, not silently redesigned for publication.

---

[Documentation index](README.md) · [Project overview](../README.md)
