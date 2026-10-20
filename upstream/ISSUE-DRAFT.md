# GP5_A10S14A5 / AK3918AV130_B: hardware notes and request for integration direction

Draft for manual review; not submitted.

We have an experimental internal-NOR port for a GP5_A10S14A5 camera, with original Linux 4.4.302-cip94, SC2B3AK/SC3336 sensor (0x2b3a), AltoBeam USB Wi-Fi and stock gv-motor/gv-gpio interfaces. The CPU is ARM926EJ-S/ARMv5TEJ; RAM is 64 MiB physically with ~26 MiB Linux MemTotal. NOR is 8 MiB with the stock six-partition layout.

The implementation includes a bounded C gateway, authenticated native SDK RTSP, OpenIPC-style WebUI, native speaker path, PTZ presets/startup position and bounded service recovery. Existing observations include authenticated short HD decoding, JPEG/preview, a controlled SDK recovery and preset persistence after internal reboot. The final feature set is not overnight/cold-power-cycle validated.

This is not proposed as an already compliant firmware PR: current vendor-process integration uses LD_PRELOAD, full-image builds still need stock/proprietary inputs, original-code licensing awaits confirmation and standard OpenIPC first-boot integration is unfinished. No proprietary firmware image or configured backup is attached.

Questions for maintainers:

- Should board identification/GPIO/motor notes first be added to builder and wiki?
- Which source-level/native SDK integration approach would be acceptable instead of preloading?
- What provenance/source route is required for the exact kernel, SDK libraries and sensor data?
- Can the existing ak3918av130 target be extended after those issues are resolved, and which validation artifacts are most useful?

Supporting material in this repository: docs/hardware.md, docs/flash-layout.md, docs/architecture.md, docs/validation.md, docs/upstream.md, hardware/board.json and manifests/. Fill in the public repository link after publication and license review.
