# Maintainer questions and precise answers

**Is this already official OpenIPC firmware?** No. It is a working experimental port and a documented source handoff. Upstream architecture, licensing, package and validation work remains.

**Does AV130 already exist upstream?** Yes. The current target/package is useful reference; the exact board, stock sensor/runtime and ABI must still be reconciled.

**Can I build a complete image from this checkout alone?** No. Our C gateway and openly licensed vendor subsets are included; the full image still needs stock/private and proprietary SDK inputs with unresolved reproducibility/distribution questions.

**Is there an SD dependency after the installed internal boot?** No SD mount/runtime dependency was observed after the recorded internal reboots. A physical cold boot with SD removed still needs a witnessed check.

**Why no ready-to-flash image?** Stock configuration contains secrets, full-image distribution requires license/provenance work, and a universal update/recovery path is not validated. The source publication must not masquerade as one.

**Is UART required for normal SSH flashing?** It was not used in the observed installation. It is a rescue route when SSH/boot fails; physical pinout/voltage must be verified separately.

**Why not use Majestic?** A working Majestic backend for this proprietary Anyka pipeline has not been established. The UI does not make unsupported Majestic controls valid.

**Is Go still required?** Not for the current native C gateway/preview. Legacy sources remain for history and useful fixtures.

**Are all camera video paths authenticated?** The public native RTSP and JPEG paths are; vendor RTSP itself ignores credentials and therefore must be kept loopback-only with mandatory isolation. Inspect actual listeners during validation.

**Can HA presets differ from WebUI presets?** They use the same camera storage/API. The current integration has a single-instance limitation.

**Are the old diagnostic programs safe to enable?** No. Frame probes/guessed gv_plat_api calls previously damaged encoder state. Historical source availability is not production approval.

**What remains most important?** Agree an upstream-compatible replacement for preload integration, settle original/vendor licenses, make full builds reproducible, and gather physical cold-boot and long-duration evidence.

---

[Documentation index](README.md) · [Project overview](../README.md)
