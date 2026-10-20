# Contributing

Read docs/status.md, docs/upstream.md and docs/licensing.md first. Record hardware revision and source/build hashes for observed behavior. Keep personal credentials, network identity and surveillance recordings out of patches and issues.

Prefer small changes with explicit ABI, memory and image-budget reasoning. Do not change stock bootloader/kernel/DT, flash a camera, expose vendor RTSP, enable guessed API hooks or run physical motor experiments as part of a documentation/code-review task. Actual deployment requires its own verified hardware/recovery gates and authorization.

Use the original component license where established; original-code licensing remains pending confirmation. Proposed upstream firmware changes must satisfy that repository's current rules, including its restrictions on preloading and binary-only artifacts. This source handoff is not an exception to those rules.
