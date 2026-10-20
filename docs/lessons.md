# Design decisions and failure history

## Why keep the original kernel and SDK?

The board's sensor/ISP, memory carve-outs, Wi-Fi and motor ABI were available and working in stock. An EV300 kernel experiment did not establish AV130 compatibility. Keeping the known kernel reduced simultaneous unknowns, at the cost of proprietary dependencies and upstream/reproducibility gaps.

## Why rewrite the Go gateway in C?

The SD version benefited from a different storage/cache environment. Stronger compression made size fit and response timing separate problems. The earlier Go runtime increased memory/GC/cache pressure in the internal compressed layout; authentication/media deadlines failed despite an apparently functional SDK. Removing WebUI alone did not settle every timing/size issue. A bounded native gateway preserved required features and reduced the userspace footprint. This result does not prove a general language performance rule.

## Why retain interposers?

They supplied mandatory vendor bind isolation, queue-depth control, AAC disabling, native RTSP/audio and small SDK fixes without replacing the closed sample. They also introduce a major conflict with current upstream's no-LD_PRELOAD rule. Documenting a successful experiment does not make its delivery mechanism upstream acceptable.

## Why not shrink every SDK buffer?

Vendor circular buffers and app queue depth have different ownership/lifetime contracts. The accepted app-frame cap is two; original 2 MiB/512 KiB/256 KiB circular buffers remain. Frame loss and memory must be measured together.

## Why separate OSD and day/night?

A blocking SDK day/night operation naturally delayed the time overlay. A dedicated small thread fixed the scheduling relationship without increasing polling pressure on ISP.

## Why stop recovery after three tries?

Repeated restarts can overlap encoder ownership, consume memory, hide hardware failure and move motors unexpectedly. Bounded graceful recovery leaves an inspectable camera when assumptions fail.

## Why are not all libraries found by ELF closure?

SDK thread cancellation loads libgcc_s dynamically. Static DT_NEEDED inspection is necessary but insufficient: exercise open/close/error lifecycles and record dynamically loaded helpers.

---

[Documentation index](README.md) · [Project overview](../README.md)
