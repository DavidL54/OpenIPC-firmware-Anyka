# ISP controls, privacy masks, detection and tracking

Native SDK controls share the existing ISP/ADC and monitor: motion ROI, MRD boxes, sound/VAD, WDR, AE compensation, white balance, 50/60 Hz anti-flicker and four opaque/mosaic privacy masks. They do not require a second competing video pipeline.

Masked cells are excluded from MRD and redacted in the NPU copy before inference. Privacy modes and static masks are different features: verify each relevant output, not only the browser preview. User configuration is per camera and excluded from public images.

Person detection uses official SVP2 HD180P with a reusable 86400-byte 320×180 DMA copy of the existing substream, about 1 Hz and confidence threshold 700. RAM guards are 5120 KiB before loading and 4096 KiB during inference. Stop on SDK errors or inference longer than 1500 ms.

Stock NPU 2.0.08 rejected the SDK ioctl. `prepare-npu.sh` loads the hash-verified SDK 2.0.25 module in RAM with exact stock vermagic, restoring stock on load failure. The mutually dependent NNE/NPU/SVP2 libraries are loaded as an ELF group. Never force-unload a module or change NOR/kernel/DTB/ion merely to suppress an ABI error.

A 30-second HD run decoded 595 frames with 33 inferences and no SDK error. **That does not prove real person recognition.** Tracking uses official `ak_mt_tracking` followed by private motor commands: eight steps, at least six seconds between corrections, 64-step boundary margin, homed marker required and five-second motion suppression. Manual move/Stop overrides tracking for 30 seconds, rechecked under the motor lock.

Tracking, sound and person detection should default off until physical validation. Motion detection state may reflect a user's explicit choice. Keep model distribution/license questions separate from the open source plugin code.

---

[Documentation index](README.md) · [Project overview](../README.md)
