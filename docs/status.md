# Status, scope and support

Snapshot: **20 October 2026**. One GP5_A10S14A5 camera has run this experimental hybrid firmware from internal NOR. The original bootloader, device tree, Linux kernel and several proprietary drivers remain essential. This is a source publication and an integration proposal, not an official OpenIPC release or a universal firmware image.

| Capability | Evidence / status |
| --- | --- |
| Internal boot, no mounted SD dependency | Observed after SSH reboot, 17–20 October |
| Public H.264 RTSP with Digest authentication | Main/sub implementation; repeated short HD decoding measurements |
| OpenIPC WebUI and browser preview | Adapted templates, C gateway and native preview |
| JPEG, Wi-Fi, SSH, image controls | Operational on the one development unit |
| Speaker PCM/MP3 | Protocol, drain and silent playback measurements; acoustic quality unverified |
| PTZ, named positions, startup selection | Installed, saved/recalled and reboot-checked on 20 October |
| Clock updated each second; bounded video recovery | Installed; short post-boot observation and RAM fault injection |
| Motion, masks, sound/person detection, tracking | Different validation levels; see detection chapter |
| Cold power-on with SD physically removed | Still requires a witnessed physical check |
| Multiple camera samples / different PCB revisions | Not established |
| Upstream acceptance / clean upstream Buildroot boot | Not established |
| Long-duration stability of the final feature set | Not established |

The earlier SD/RAM port, failed compressed Go candidates and current native C deployment are separate stages. A result from one stage does not validate another. Historical scripts are provided for understanding and review, not automatic installation.

There is already an AK3918AV130 target in OpenIPC. The contribution concerns this board, its stock-compatible runtime, missing integration details and reproducible evidence; it does not introduce Anyka support from zero.

---

[Documentation index](README.md) · [Project overview](../README.md)
