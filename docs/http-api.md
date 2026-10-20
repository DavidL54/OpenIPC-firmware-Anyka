# HTTP API and preset contract

The route allowlist in `native-gateway/http.c` is authoritative. This is the experimental camera API, not a claim of compatibility with Majestic's full API or ONVIF.

| Route | Use |
| --- | --- |
| POST /cgi-bin/access.cgi | Web login/password form; session cookie and redirect |
| POST /logout | Revoke session |
| GET /image.jpg or /cgi-bin/snapshot.cgi | JPEG, session or Digest |
| /cgi-bin/metrics.cgi | Authenticated runtime metrics |
| /cgi-bin/isp.cgi | Native ISP/detection states and allowed controls |
| /cgi-bin/gpio.cgi | Allowed board GPIO/day-night controls |
| /cgi-bin/ptz.cgi | Position, bounded movement and Stop |
| GET/POST /api/ptz/presets | Camera-owned named positions |
| GET/POST /api/privacy | Camera privacy state |
| GET /api/ws | WebSocket browser media, UI profile |
| GET /api/audio/live | Authenticated WebSocket PCM/MP3 transport |
| POST /api/audio/play, /api/audio/stop | Bounded playback / cancel |
| /api/time, /api/ntfy | UI time/notification helpers |

Do not guess CGI query parameter names: consult the included scripts and matching HA bridge for each action. Only the implemented action allowlist is safe. Unknown administration/flash/console endpoints return 404.

## Presets

GET has no query string and requires a web session. It returns version, startup slot, saved positions, current position (or null) and homed status. POST uses exact content type `application/x-www-form-urlencoded`, no query, at most 1024 bytes, same-origin and a valid session.

| action | Fields | Semantics |
| --- | --- | --- |
| save | id=1..8, name | Read current calibrated position, replace slot |
| recall | id=1..8 | Move to saved position, bounded and interruptible |
| delete | id=1..8 | Remove slot, clear startup if selected |
| startup | id=0..8 | Select existing slot or 0 for center only |

A 48-byte UTF-8 name maximum applies. The API does not accept arbitrary uploaded coordinates for `save`. Typical errors: 400 invalid form/id/name, 401 session required, 404 missing slot, 409 unhomed/busy/interrupted, 503 invalid configuration or failed movement/persistence. Clients must not retry a timed-out recall automatically: the camera may already have moved.

The saved-file schema and preset OpenAPI subset are in `api/`. The OpenAPI subset deliberately describes presets only; it is not a fabricated complete schema for every SDK control.

---

[Documentation index](README.md) · [Project overview](../README.md)
