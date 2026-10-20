# Validation plan for a future release

This is a proposed protocol. No result below should be marked passed until recorded for a specific image hash and camera revision.

## Build and static checks

Record host/toolchain versions, exact upstream/SDK source revisions, all input hashes, licenses, build flags, output sizes and ELF reports. Check ARMv5 soft-float, interpreter/DT_NEEDED, dynamic cancellation dependencies, plugin libc boundary, exact module vermagic, public route allowlist and no embedded credentials. Check both runtime and public-source manifests in their proper scopes.

## Boot matrix

Cold boot stock and candidate, SD inserted/removed, warm reboot and service-only restart. Record `/proc/mtd`, mounts, kernel, sensor ID, Wi-Fi, DHCP, lo/listeners, private socket modes, marker state and protected hashes. Boot a deliberately invalid SD candidate with the documented failover path. Observe physical recovery separately; do not infer it from a successful SSH session.

## Media and concurrency

Decode authenticated HD/sub for at least 24 hours, record frame/timestamp errors and memory/log growth. Repeat with one browser preview, up to two public RTSP readers, HA reconnect, slow/blocked reader, network loss, password rotation, incorrect credentials and client timeout. Check no third public peer is admitted, initial audio/video sync, rendered OSD tick rate and clean release of every slot.

## Audio and controls

Check real acoustic voice quality, stop/EOF/drain, amplifier mute on disconnect/error, expired sessions, mono/stereo MP3 source rates, five-minute/20 MiB caps and memory under simultaneous HD. Validate every GPIO/ISP setting, privacy output, UTF-8 preset, startup-once and Stop during long movement. Keep people clear of the mechanism and stop on abnormal mechanical behavior.

## Detection, persistence and recovery

Use controlled visible subjects for motion/person and masked regions; observe tracking boundaries/manual override. Test malformed/truncated config, full JFFS2, write errors and read-only restoration without distributing private contents. Exercise bounded SDK recovery and exhaustion; verify no automatic reboot/rehoming/forced unload. A hardware-assisted power-cut persistence test requires its own recovery preparation and is not implied by this checklist.

Report failures and limits alongside successes. Preserve short sanitized log excerpts, performance samples and hashed build/observation metadata. Do not include surveillance scenes, sound recordings, LAN identities or credentials in public artifacts without explicit review.

---

[Documentation index](README.md) · [Project overview](../README.md)
