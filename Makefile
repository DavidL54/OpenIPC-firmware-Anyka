.PHONY: help audit gateway
help:
	@echo 'Source publication: make audit; make gateway (requires configured ARMv5 musl toolchain)'
	@echo 'No full firmware target, installer, flash, deployment or push target is provided.'
audit:
	python3 tools/audit-publication.py
gateway:
	python3 native-gateway/build.py --output out/native-gateway
