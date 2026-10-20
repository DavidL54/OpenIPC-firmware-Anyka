# PTZ motor ABI and safe positioning

The camera uses stock `/dev/gv-motor`, not SDK `/dev/motor0` or `/dev/motor1`. Opening/closing the device stops both axes, which makes concurrent independent opens unsafe. Commands must share serialization and allow Stop to interrupt a longer recall.

| ioctl | Hex value |
| --- | --- |
| INIT | 0x40044d6e |
| MOVE | 0x40044d70 |
| POSITION | 0x40044d74 |
| STATUS | 0x40044d76 |

MOVE uses four integers `{axis, direction, steps, 0}`. Axis 0 is pan, 0–4039; axis 1 is tilt, 0–967. Direction 1 decrements the counted coordinate and 0 increments it. Up/down mapping in the application was corrected and confirmed on the development unit; do not infer orientation for a different mechanical assembly.

Manual movements are capped at 32 steps and three seconds. Startup mode 1 homes by counted travel (4039/967), with 120-second timeout. Nominal center is 2019/483; the validation accepts the observed small center tolerance (pan 2017–2021, tilt 481–485). The installed unit commonly reports 2017/482.

This is counted-travel homing, not proven physical endstop detection. A RAM homed marker is created only after center validation. A valid marker/position avoids rehoming on a WebUI restart. Unhomed motion is disabled. Native SDK reset-to-home must not run concurrently.

Long recalls use chunks of at most 32, a helper deadline of 60 seconds and gateway supervision around 65 seconds. Stop creates cancellation state rather than waiting for the full recall lock. A controlled recall was interrupted with helper exit 5, then position was restored. Position counts can drift after stalls; saved coordinates are not encoders.

---

[Documentation index](README.md) · [Project overview](../README.md)
