# Image packaging and reproducibility limits

The internal builder assembles a stock-compatible rootfs and application image. It does not generate a replacement kernel/DT/U-Boot. It selects runtime files, constructs the internal directory views, stages musl tools and preserves uClibc dependencies. Configuration is per camera and must never be copied into a public release.

`internal-firmware/build-images.py` has deliberate pinned private inputs: the original full NOR backup, a known stock rootfs extraction, a known OpenIPC SD userland image and the runtime manifest. These files are excluded. Paths and hashes are historical safety checks, not generic defaults to bypass.

The rootfs maximum is 983040 bytes and app maximum 4849664 bytes. Verify the actual compressed outputs and flash geometry, not source folder size. Account for full-block erase/write padding and exact installed hashes. JFFS2 configuration has only 393216 bytes; transient logs and periodic state do not belong there.

The large earlier Go gateway exceeded the compressed budget and showed timing failures from SquashFS/cache on this small-RAM target. The current C rewrite addresses memory, startup and storage pressure while preserving SDK controls. It is not a blanket claim that every Go program fails from NOR.

A release that another maintainer can reproduce still needs:

1. Redistributable and version-pinned SDK/runtime acquisition with clear licensing.
2. Exact corresponding kernel/module sources or an agreed supported driver distribution route.
3. Deterministic extraction of required stock files without borrowing a configured camera image.
4. Buildroot package recipes and CI registered in the upstream build tree.
5. Clean configuration provisioning with no preinstalled password, Wi-Fi or host identity.
6. Repeated cold-boot and recovery validation on physical hardware.

`manifests/lab-runtime-reference.sha256` records the private lab runtime. Missing entries in this source-only tree are intentional: do not run it as the export checksum manifest. Use `manifests/PUBLICATION.sha256` for this publication.

---

[Documentation index](README.md) · [Project overview](../README.md)
