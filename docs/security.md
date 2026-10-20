# Authentication and exposure model

The camera is intended for a trusted LAN or access through an external VPN. HTTP, RTSP and RTP are not encrypted by Digest. Do not present HTTP Digest as TLS or publish the raw camera ports on the internet.

The gateway stores a bcrypt password hash (accepted cost 8–12) and Digest HA1 in private configuration. **HA1 is password-equivalent for its realm** and must receive the same protection as a password. Neither HA1 nor credentials are bundled into firmware, exposed in browser JavaScript or passed to the SDK. Home Assistant credentials remain in its server-side config entry.

Web sessions use a random token, `HttpOnly; SameSite=Strict` cookie, approximately eight-hour lifetime and explicit invalidation. Every modifying route requires the allowed action and same-origin POST. Native RTSP requires Digest including localhost, uses qop/replay controls, and has no anonymous/Basic downgrade. Password change closes public peers. Audio checks session expiry/revocation during streaming, not only at handshake.

Private Unix sockets reside in `/conf/anyka-private` with 0700 parent and 0600 sockets. The SDK asks the gateway to authorize RTSP without receiving the underlying secret. Closing the authorization connection revokes that peer. Vendor :8555 and CGI :8080 remain loopback-only.

Credential persistence is serialized, uses exclusive no-follow temporary files, fsync/rename and directory synchronization, then attempts to restore the configuration mount read-only on each write path. Filesystem errors must be surfaced instead of silently claiming a saved password or position.

The implementation has bounded parsers and clients, but it has not received an independent security audit. Kernel/SDK age, root-owned hardware access, preloading, cleartext LAN protocols and custom credential provisioning are integration risks. The upstream OpenIPC first-boot ownership model must replace the experimental provisioning model before a normal upstream release.

Public bug reports should contain sanitized logs and version/hash facts. Never attach web-access.json, Wi-Fi profiles, complete configuration partitions, host private keys or full flash dumps.

---

[Documentation index](README.md) · [Project overview](../README.md)
