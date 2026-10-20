# Build the sources

## Standalone C gateway

Prerequisites: Linux host, Python 3, an ARMv5 soft-float musl cross toolchain with pthread/libcrypt support and ARM binutils. The code and vendored cJSON/mbedTLS subsets needed by this component are included.

```sh
export CC=/opt/anyka-toolchain/bin/arm-openipc-linux-musleabi-gcc
export STRIP=/opt/anyka-toolchain/bin/arm-openipc-linux-musleabi-strip
export READELF=/opt/anyka-toolchain/bin/arm-openipc-linux-musleabi-readelf
python3 native-gateway/build.py --output out/native-gateway
```

Use a fresh output directory: the builder preserves an existing executable. `--static` chooses a static musl gateway; `--headless` records the no-WebUI profile. Neither option builds the closed SDK or deploys anything. The script checks bundled vendor hashes and emits ELF-REPORT, LINK-MAP and PROFILE reports. Its clock compatibility `.so` remains a separate musl helper even for a static gateway.

## SDK native components

Review the `rtsp-bridge/build-anyka-*.sh` sources before use. These lab scripts expect the AnyCloud SDK at the project-root directory `AnyCloud39AV130_SDK_V1.03`, and the original cross toolchain at the sibling `openipc-ak3918-build/output-toolchain-ak3918ev300/host/bin` path. Arrange an external SDK symlink or adapt those explicit paths in a separate build workspace; do not commit the SDK.

Build `anyka-ptz`, GPIO, auth isolation, native RTSP and day/night components with their corresponding scripts. Day/night compiles SDK `sample/mt/ak_mt.c` and links the NPU ELF group: this requires SDK source and binary inputs not distributed here. Native RTSP includes SDK headers. These are not self-contained builds merely because our plugin sources are present.

## BusyBox

Obtain BusyBox 1.36.1 from its official archive; verify the pinned hash in dependencies.json. Set `ANYKA_CC_PREFIX` to the cross prefix and use:

```sh
python3 internal-firmware/build-busybox.py --source /opt/sources/busybox-1.36.1.tar.bz2 --output out/busybox
```

The helper uses Python's safe tar extraction API (`filter='data'`), make and patch. `--rescue-tools` produces a separate static maintenance variant containing flashcp; it is not the normal runtime.

## Runtime setup address

Publication sanitization replaces the historical setup-computer IP default with the RFC 5737 address `192.0.2.20`. Before an intentional private first-setup deployment, configure `ANYKA_SETUP_IP` for the actual authorized installation computer. The exported default is deliberately not a usable LAN configuration. This is separate from the unresolved upstream standard first-boot integration.

## Full images

Read the packaging chapter before invoking build-images.py. It needs private stock inputs, the external SDK runtime and exact manifest pins. No source-only checkout can currently reproduce a complete bootable image. No full image build or hardware test was performed for this documentation export.

---

[Documentation index](README.md) · [Project overview](../README.md)
