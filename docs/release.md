# Source publication and future release procedure

This folder is ready for local review as a documentation/source handoff, subject to the explicit pending license decision. It is not a signed, installable or upstream-approved firmware release. No remote push or maintainer message is part of this preparation.

Before making a public commit, inspect the complete Git diff, including any pre-existing deletions inherited from the user's clone. The destination originally contained an OpenIPC firmware Git history with most build-tree files deleted. This documentation export preserves that state; it does not reconstruct a complete upstream Buildroot tree. Existing `.github` workflows belong to that inherited tree and may not make sense for a standalone source handoff.

For a clean standalone publication, create a reviewed source-only branch or repository and decide deliberately whether to retain inherited CI. Do not trigger an upstream firmware/image workflow merely to validate Markdown. The provided Makefile only offers local source audit and the standalone gateway build; it does not flash, deploy or push.

Release gates:

- Confirm the original-code license and all third-party notices.
- Run the local source audit and verify PUBLICATION.sha256.
- Review changes from originals in source-export.json.
- Exclude local backups, credentials, SDK artifacts, camera screenshots and per-camera evidence.
- Check links, versions and upstream policy at the time of publication.
- Add image build reports and fresh physical observations only if distributing an actual firmware release.

A future binary release should ship separate source, build recipe, exact dependency/license inventory, image layout/size/hash report, sanitized validation report and recovery documentation. It must not include a configured JFFS2 partition or the camera's private NOR backup.

---

[Documentation index](README.md) · [Project overview](../README.md)
