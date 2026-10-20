# Historical SD/RAM deployment

The original validated approach kept stock bootloader, kernel, DT and rootfs and executed OpenIPC musl tools and the vendor uClibc SDK from SD. It was the development/recovery route before the internal native C firmware.

The filtered scripts are retained in `sd-openipc-test/` and `reference/sd-workflow/`. The binary SD images, stock kernel/DTB dumps, runtime libraries and per-camera network settings are excluded. This source checkout is not a ready-to-write SD bundle.

`prepare-openipc-sd.sh` repartitions a removable disk and destroys its content. Its former relative directory layout is archived, not directly runnable from the reference directory. Reconstruct and inspect an SD bundle in a separate private workspace first; identify the exact removable block device, size and current mounts before any write.

The safe development principle is to boot the original kernel and drivers, then load candidate userspace and selected exact-version SDK modules in RAM. Changing SD userland must not change NOR or persist U-Boot environment. Keep FAT read-only after deployment; synchronize and remount after an explicit user setting change.

Early EV300 kernel/rootfs attempts were incompatible with the AV130 target and are excluded. Stock RTSP exposed no usable media track. Neither is a recovery fallback.

The persistent SD supervisor could create `boot-stock.flag` for failover, then sync/remount read-only. If it cannot persist a failover flag, it must not reboot into a loop; power off and remove the SD instead. That mechanism belongs to the older SD workflow, not a claim of redundant A/B NOR firmware.

---

[Documentation index](README.md) · [Project overview](../README.md)
