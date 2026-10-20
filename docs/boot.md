# Internal boot and filesystem views

1. Original U-Boot loads the original DT and Linux kernel.
2. Internal rootfs initialization creates RAM state and mounts application/configuration partitions.
3. Configuration is made available privately; persistent settings normally stay read-only.
4. Stock-compatible hardware drivers and the exact sensor configuration are loaded.
5. Loopback is brought up **before** the SDK listener/auth interposer.
6. The SDK starts with the bounded encoder queue and required native components.
7. The C gateway and loopback CGI backend start after their prerequisites.
8. PTZ homes once if needed, then applies a selected saved position once per boot.
9. The bounded monitor observes readiness and can recover the SDK without repeating PTZ homing.

Internal deployment reuses historical paths. `/media` is a link to `/app/tools`; it is not evidence that an SD is mounted. `/mnt/openipc-persist/sdk-rtsp` is a real directory view with links into `/app/runtime/sdk-rtsp`. Relative paths used by CGI must resolve as expected; replacing every real directory by a symlink previously broke assumptions.

`/conf/anyka-private` is private RAM state, mode 0700. Native audio and RTSP auth sockets are mode 0600. Runtime state, logs, health counters and PTZ homed/startup markers belong in RAM. Durable settings are coordinated, synchronized and remounted read-only after a user change.

The source `start-web.sh` and monitor retain SD-era go2rtc names. For native-C images, `build-images.py` rewrites those scripts to launch/monitor `anyka-gateway`, removes the legacy relay configuration and installs the native preset startup hook. Do not mistake an untransformed source script for the installed C image.

No U-Boot `saveenv` or new bootloader is needed for the installed experiment. The published source does not include a universally applicable boot installer.

---

[Documentation index](README.md) · [Project overview](../README.md)
