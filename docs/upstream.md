# Official OpenIPC integration plan

Reference review: 20 October 2026, against current official OpenIPC firmware contribution guidance and the existing AV130 target. Recheck those documents at submission time; this page does not override maintainer policy.

| Contribution | Expected upstream destination |
| --- | --- |
| Hardware identification, GPIO/motor mapping, retail board overlay | OpenIPC/builder |
| General hardware/how-to documentation | OpenIPC/wiki / docs.openipc.org |
| Kernel source changes and patches | OpenIPC/linux |
| Shared Anyka packages, defconfig integration, load scripts | OpenIPC/firmware |
| Camera identification/probe extensions | OpenIPC/ipctool |
| Experimental HA custom integration | This repository or a separate integration project; not implicitly HA core |

Existing references: `br-ext-chip-anyka/configs/ak3918av130_lite_defconfig`, `general/package/anyka-osdrv-ak3918av130`, and its `files/script/load_anyka`. The inspected upstream base is recorded in upstream/BASELINE.json. Existing support does not establish support for this exact retail unit/stock module set.

See [existing upstream target comparison](upstream-existing-target.md) for the documented module source commit and distinction between upstream ABI work and this stock-kernel deployment.

## Acceptance gaps

1. **LD_PRELOAD is prohibited in current shipped upstream scripts/packages.** Agree an explicitly linked SDK integration, source-level fix or another maintainer-approved architecture. Do not submit the current preload stack as compliant.
2. Establish reproducible/legal acquisition of SDK runtime, sensor/model assets and corresponding kernel/module sources.
3. Implement normal OpenIPC unclaimed first-boot and shared identity ownership; remove personal network defaults from shared code.
4. Put GPIO/sensor/board defaults in a device-specific builder overlay, not generic Anyka initialization.
5. Develop scoped package recipes and dependencies in the real Buildroot tree; register configurations with upstream CI matrix. Current upstream Makefile pins Buildroot 2024.02.10 in the reviewed baseline.
6. Verify replacement-kernel or stock-kernel support explicitly and document update/recovery compatibility. No generic sysupgrade recipe is validated here.
7. Confirm original-code license and maintainership; attach fresh physical cold-boot/media evidence.

## Suggested sequence

Start with a hardware evidence/documentation issue using the prepared draft. Ask maintainers which SDK integration approach they will accept. Submit board identification and reusable small fixes separately. Only then propose the runtime package and image configuration. Keep experimental working firmware and upstream proposals distinctly labeled throughout review.

Primary references:

- [Firmware contribution guidance](https://github.com/OpenIPC/firmware/blob/master/CLAUDE.md)
- [Best practices](https://github.com/OpenIPC/firmware/blob/master/best_practices.md)
- [Firmware source](https://github.com/OpenIPC/firmware)
- [Builder](https://github.com/OpenIPC/builder)
- [Linux](https://github.com/OpenIPC/linux)
- [Wiki](https://github.com/OpenIPC/wiki)
- [Supported hardware](https://openipc.org/supported-hardware/full-list)

---

[Documentation index](README.md) · [Project overview](../README.md)
