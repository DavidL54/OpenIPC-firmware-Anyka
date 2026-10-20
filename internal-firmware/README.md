# Internal firmware laboratory builders

These sources assemble the stock-compatible internal userspace and document the installed architecture. They require excluded private stock inputs and SDK runtime assets. This source-only publication cannot build a complete flash image by itself.

Read [packaging](../docs/packaging.md), [boot](../docs/boot.md), [installation gates](../docs/flashing-recovery.md) and [licensing](../docs/licensing.md) before using any builder/preparation/deployment source. No flash action is part of source publication.

The fixed-board SSH transition scripts are preserved under reference/lab-ssh-maintenance instead of being presented as active installer defaults. Legacy Go reduction files remain for historical analysis. Current production gateway/preview is native C.
