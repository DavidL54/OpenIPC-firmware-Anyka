# Wi-Fi, addressing and provisioning

The development unit uses the original AltoBeam USB Wi-Fi driver. Stock-compatible kernel modules and firmware must match the running kernel. This port does not replace the Wi-Fi driver with a guessed generic OpenIPC module.

Bring loopback up before starting SDK/private listeners. A missing lo caused the auth listener startup to fail and SDK CPU to rise to about 96%; restarting with lo up restored roughly 1–3% SDK CPU in that observation. A full cold-boot validation of that exact diagnostic remains separate.

Use DHCP plus a reservation at the router if a stable LAN address is needed. The development camera's earlier address change was handled through a DHCP reservation; it did not require reflashing. Do not hardcode the author's LAN into shared firmware. All `192.0.2.x` addresses in this repository are non-routable documentation examples, not deployment defaults.

Wi-Fi SSID/PSK, SSH host keys/authorized keys, web password hash/HA1 and per-camera settings are private provisioning inputs. They must be generated or entered on the target, never checked into a public image. The historical staging script illustrates copying private configuration, not a public default credential.

The historical wpa_supplicant compatibility patch uses CLOCK_REALTIME because of the old kernel/userspace time ABI. Review time-change behavior and seek a proper upstream-compatible fix before applying that patch generally.

Upstream integration needs the standard OpenIPC first-boot claim/ownership workflow and shared identity support. The experimental private web configuration and manual SSH authorization do not yet implement that complete contract.

---

[Documentation index](README.md) · [Project overview](../README.md)
