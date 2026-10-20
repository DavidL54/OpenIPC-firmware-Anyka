# Home Assistant integration and card

This is a custom integration, version **0.3.0**, separate from official OpenIPC firmware support and separate from Home Assistant core. It proxies camera controls through Home Assistant's authenticated HTTP/WebSocket backend. The camera owns all persistent positions.

## Manual installation

1. Copy the directory `homeassistant/anyka_controls` into your HA `/config/custom_components/anyka_controls` directory. Preserve the administrator's normal file ownership.
2. Copy `homeassistant/anyka-controls-card.js` into `/config/www/anyka-controls-card.js`.
3. Restart Home Assistant to load the integration. Add **Anyka Controls** in Devices & services, entering the camera IP and its password server-side.
4. Register `/local/anyka-controls-card.js?v=0.3.0` as a JavaScript module in dashboard resources.
5. Add the custom card:

```yaml
type: custom:anyka-controls-card
```

The current config flow uses one fixed unique ID and the card uses fixed entity names. Multi-camera installations require further work; do not claim automatic discovery or unlimited independent instances. Host input is an IP address rather than arbitrary URL. The integration does not replace HA's normal RTSP camera setup.

Camera preset GET/POST is proxied at `/api/anyka_controls/presets`. Admin checks protect the route. The recall deadline is 70 seconds; movement does not hold the bridge's login/image lock, leaving Stop available. A failed/timeout response is not automatically retried. The HA card exposes save, recall, delete and startup selection for eight slots using the camera's shared database.

Speaker streaming uses the existing authenticated backend and cancellation semantics; no camera password is embedded in the card. A browser microphone typically requires an HTTPS/secure HA origin and user microphone permission. Audio quality remains a physical validation item.

The 20 October deployed checks saw admin GET 200, startup=0 preserved, invalid slot 9 rejected with 400, preset and image states present, updated card resource HTTP 200, and 201 video frames in a ten-second diagnostic. These observations do not prove every future HA release is compatible.

Troubleshooting: verify the resource URL/version, refresh the browser cache, inspect HA logs without credentials, check camera login directly, and close extra RTSP diagnostic readers. Do not expose camera credentials in dashboard YAML or screenshot the integration's private config data.

---

[Documentation index](README.md) · [Project overview](../README.md)
