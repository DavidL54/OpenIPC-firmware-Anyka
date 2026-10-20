# Licenses, attribution and publication gates

## Original port code

The choice of license for newly written original port code is **awaiting the project owner's confirmation**. This local export does not silently apply the inherited OpenIPC repository license to unrelated new work. Until confirmed, original files without an existing license notice are not offered under a new blanket open-source license. Resolve this before publishing a release or requesting code integration.

Existing third-party notices remain in force regardless of that choice. Derived code, notably the preview muxer adapted from go2rtc, retains the upstream MIT terms and attribution. Do not remove notices or replace vendor licenses with the project's future choice.

## Included material

| Component | License / notice |
| --- | --- |
| cJSON 1.7.19 | MIT, bundled LICENSE/UPSTREAM and exact source |
| mbedTLS subset 3.6.4 | Apache-2.0 OR GPL-2.0-or-later, bundled upstream license/provenance |
| go2rtc-derived C mux behavior/code | MIT, origin paths and hashes in vendor/go2rtc/ORIGIN.txt |
| OpenIPC firmware reference notice | MIT, original 2021 OpenIPC notice retained separately |
| OpenIPC WebUI templates/assets | MIT base project notice retained; individual asset notices also apply |
| wav-encoder asset | Preserve adjacent wav-encoder.LICENSE |
| curl/zlib runtime provenance | Notices retained under internal-firmware/licenses; runtime binaries excluded |
| BusyBox configuration/patch and build recipe | BusyBox is GPL-2.0-only; obtain matching source and retain obligations in any binary release |

The SPDX/CycloneDX inventory uses NOASSERTION where a component or exact derivative's grant has not been established. A version/hash is not a copyright permission.

## Excluded vendor inputs

Public availability of the AnyCloud SDK on GitHub is not blanket permission to redistribute its proprietary libraries, samples, model weights or ISP data. No such grant was found for the full SDK. This repository references and fingerprints the laboratory runtime instead of bundling those files.

The stock Linux/kernel modules and U-Boot also require a corresponding-source/license assessment. A nearby SDK source tree is not proof that it corresponds exactly to stock Linux 4.4.302-cip94. Do not label an entire stock firmware dump MIT, GPL or redistributable merely because it contains some open-source programs.

Before an upstream request: confirm original-code licensing and ownership, review SDK sample-derived portions, establish legal distribution/acquisition for every runtime input, include required source offers/notices for distributed binaries, and have a maintainer review the result. This is an unresolved engineering/provenance checklist, not a claim of legal clearance.

---

[Documentation index](README.md) · [Project overview](../README.md)
