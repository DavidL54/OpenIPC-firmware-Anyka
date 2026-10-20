# Existing upstream AV130 work and comparison

Reference: OpenIPC/firmware commit `97d1c26364e752ff13fafed1a89590cc987be9a3`. The four text files under `upstream/reference/` are preserved as evidence of the existing target; they are not a complete Buildroot tree or newly written package recipes.

## What already exists

The `ak3918av130_lite_defconfig` selects ARM, external musl tooling, Linux, the Anyka AV130 driver package, standard OpenIPC services and Majestic WebUI. Its family configuration lives under the historical `ak3918ev300` family directory. This is a path convention, not evidence that the AV130 CPU executes ARMv7 instructions.

The driver package is explicitly marked PROPRIETARY in its recipe. Its comments document the module origin as the public SDK repository `https://github.com/Link-WQS/ANYKA_130L_SDK`, commit `3480d024b682ccb2cf8bcb87dc4d2ac0af4dc49e`, from `os/driver_normal.tar.gz` / `driver/external`. This is a documented upstream reference, not a verified revision identity for every local SDK input used in this experiment.

The same package comments explain that the OpenIPC kernel branch imports the AV130 machine and adjusts vendor-facing structure layouts. The `-cip94` release suffix provides matching vermagic; it does not turn the upstream mainline-derived tree into the actual CIP stock kernel. The comments report symbol resolution and 275 structure-size/member-offset comparisons against the SDK tree. They explicitly distinguish that static work from a physical boot and say AV130 remains outside ALL_BOARDS until the modules load and run on a camera.

## Why this experiment still matters

This development unit has recorded working stock-kernel media and motor behavior, a sensor/configuration identification, a custom C userspace and an observed internal partition layout. Those observations can guide physical validation of upstream work. They do not independently validate the different OpenIPC kernel or prove every module's structure ABI is identical.

| Area | Existing target | This experiment |
| --- | --- | --- |
| Kernel | OpenIPC branch with AV130/vendor ABI adjustments | Actual stock Linux 4.4.302-cip94 retained |
| Module provenance | SDK archive and commit noted in package | Runtime files fingerprinted in excluded-assets inventory |
| Video application | Generic target/package scaffolding | Working vendor sample plus native extensions, preload delivery |
| Retail-board controls | Shared target is not a retail-board overlay | Observed GPIO, motor, sensor and flash mapping |
| Validation | Package comments document static ABI work, still require board boot | Short media/control observations on stock kernel |

Coordinate with maintainers rather than submitting duplicate SoC support. A useful next step is a separate physical upstream-kernel bring-up in a prepared recovery environment, followed by narrow board/runtime contributions that satisfy current policy. No such kernel boot was attempted during this source publication.

[Documentation index](README.md) · [Upstream plan](upstream.md)
