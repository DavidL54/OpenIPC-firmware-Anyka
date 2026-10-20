# ARMv5 and libc compatibility

The camera CPU cannot run generic ARMv7 Go releases or arbitrary OpenIPC shared libraries. Compile C with `-march=armv5te -marm -mfloat-abi=soft`. Inspect ELF headers, ARM attributes, interpreter, DT_NEEDED and undefined symbols, rather than trusting filenames.

| Component | Runtime contract |
| --- | --- |
| Native HTTP gateway | musl process; existing arm musl loader or explicit static build |
| haserl / selected utilities | musl process tree |
| Vendor sample, ISP/audio libraries | original uClibc |
| SDK interposers | ARMv5, `-nostdlib`, original uClibc `dlsym`, static libgcc arithmetic helpers |
| Kernel modules | exact running kernel vermagic and required SDK versions |
| curl clock compatibility helper | musl process only, not SDK |

The build script's ELF checks require the gateway interpreter `ld-musl-arm.so.1` and only libc as a shared dependency for the dynamic profile. SDK plugin builds must not acquire musl dependencies, even if a musl cross-compiler is used to emit freestanding code.

A particularly important dependency is original uClibc `libgcc_s.so.1`. SDK cancellation can load it dynamically, so a DT_NEEDED-only closure misses it. Removing it caused a fatal SDK exit on speaker close. The observed required library hash is `b8b875a1b1c35dcb9c6aa6b67932b2e2c1a92a461181c4fcd27b9eefa507b8ff`.

The Linux 4.4 time syscall ABI also differs from assumptions made by some modern musl tools. The compatibility source and the historical wpa_supplicant CLOCK_REALTIME patch are included for inspection. Realtime clock jumps are a limitation of that workaround; it is not a recommended generic upstream time implementation.

---

[Documentation index](README.md) · [Project overview](../README.md)
