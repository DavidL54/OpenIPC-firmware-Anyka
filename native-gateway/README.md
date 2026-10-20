# Native C gateway

Current HTTP/auth, browser preview, speaker transport and preset implementation for the experimental GP5_A10S14A5 port.

- [Source map](../docs/source-map.md)
- [Build instructions](../docs/build.md)
- [API](../docs/http-api.md)
- [ABI boundaries](../docs/abi.md)
- [Security](../docs/security.md)
- [Licensing status and retained notices](../docs/licensing.md)

No deployment or NOR write is performed by build.py. Publication changes make CC/STRIP/READELF and ANYKA_TOOLCHAIN_DIR configurable and replace the lab setup IP with a documentation address; set ANYKA_SETUP_IP explicitly for a private deployment. See manifests/source-export.json for source hashes and exact scope.
