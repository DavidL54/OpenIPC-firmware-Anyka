# NOR partitions and ownership

All offsets below are for the observed 8 MiB unit. Read `/proc/mtd`, DT and actual erase geometry on every other unit.

| MTD | Offset | Size | Bytes | Role |
| --- | --- | --- | ---: | --- |
| 0 | 0x000000 | 0x040000 | 262144 | Bootloader / environment region |
| 1 | 0x040000 | 0x010000 | 65536 | Device tree |
| 2 | 0x050000 | 0x1c0000 | 1835008 | Stock kernel |
| 3 | 0x210000 | 0x0f0000 | 983040 | Root filesystem |
| 4 | 0x300000 | 0x4a0000 | 4849664 | Application filesystem |
| 5 | 0x7a0000 | 0x060000 | 393216 | Configuration filesystem |
| 6 | 0x000000 | 0x800000 | 8388608 | Whole-chip overlapping alias |

**MTD6 overlaps all partitions. It is not an additional free writable area.** The hexadecimal sizes of MTD0–5 sum exactly to 8 MiB. The installed experiment preserved MTD0/1/2. Rootfs and application are SquashFS; configuration is JFFS2, normally read-only.

A normal OpenIPC image, generic sysupgrade, arbitrary partition enlargement or an EV300 kernel must not be assumed compatible. The stock boot arguments, partition names and device tree remain part of this deployment contract.

No private full-chip image is distributed here. A full dump can contain network credentials, password hashes, host keys and device identities. Store a fresh recovery copy outside this public tree and check its size and hash privately.

The JSON counterpart is `hardware/board.json`. This page documents the existing deployment; it grants no permission to write a different camera.

---

[Documentation index](README.md) · [Project overview](../README.md)
