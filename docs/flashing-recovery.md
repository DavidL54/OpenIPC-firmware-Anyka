# Installation gates and recovery

This publication deliberately provides **no one-command NOR installer**. The included private-lab builders and flash preparation sources document how a specific development unit was handled. Their hashes, process IDs and partition assumptions cannot authorize or validate another unit.

Before any future installation:

1. Identify hardware revision, CPU, kernel, erase geometry, partitions and active mounts from fresh read-only observations.
2. Make a private complete flash backup and separate partition backups; verify sizes/hashes and a known recovery route. A backup file alone does not prove it can be restored.
3. Inspect the entire candidate: exact ELF/ABI closure, mandatory auth interposer hashes, kernel module versions, sensors, filesystem structure, compressed sizes and no private config bundled.
4. Exercise the candidate in SD/RAM with authenticated HD/JPEG/preview/audio/PTZ under actual resource limits; measure available RAM and bounded logs.
5. Define exactly which partitions change and compare protected partition hashes. Preserve bootloader/environment, DTB and kernel for this architecture.
6. For SSH installation, transition the code, loader, libraries, shell, SSH and maintenance tools needed for completion into independent RAM execution. Never overwrite a mounted executable filesystem still needed by the flashing process.
7. Read back programmed data, compare against the expected padded image and restore configuration read-only. Reconnect, inspect boot logs, authenticate and decode media.
8. Physically cold boot with the SD removed when safe, then perform extended observation and keep a tested recovery route.

SSH can disappear if networking, memory, a process dependency or power fails. There is no atomic dual-bank update here. UART/SPI are physical rescue options, not required for every successful SSH flash; pinouts, voltage and chip wiring still need independent verification. The hardware was not used during the observed successful SSH installation.

Never run `saveenv`, erase the whole-chip alias, force-unload active video modules or replay an old fixed-PID maintenance script. A later GPIO/PTZ or WebUI change does not justify reflashing bootloader/kernel.

If the camera remains reachable but video fails, prefer logs, dependency checks and a bounded graceful restart. If boot fails, preserve evidence and recover with the verified hardware procedure for that exact board; do not publish guessed UART/SPI wiring.

---

[Documentation index](README.md) · [Project overview](../README.md)
