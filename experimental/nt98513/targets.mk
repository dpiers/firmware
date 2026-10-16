# Explicit opt-in targets bypass OpenIPC's shared fragment and flash repack.
NT98513_BUILDROOT ?= $(error Set NT98513_BUILDROOT to pinned Buildroot 2024.02.10)
NT98513_OUTPUT ?= $(error Set NT98513_OUTPUT to a dedicated output directory)
NT98513_EXTERNAL := $(CURDIR)/experimental/nt98513
.PHONY: nt98513-ram-defconfig nt98513-ram-source nt98513-ram
nt98513-ram-defconfig:
	$(MAKE) -C $(NT98513_BUILDROOT) O=$(NT98513_OUTPUT) BR2_EXTERNAL=$(NT98513_EXTERNAL) nt98513_ram_defconfig
nt98513-ram-source: nt98513-ram-defconfig
	$(MAKE) -C $(NT98513_BUILDROOT) O=$(NT98513_OUTPUT) BR2_EXTERNAL=$(NT98513_EXTERNAL) source
nt98513-ram: nt98513-ram-defconfig
	$(MAKE) -C $(NT98513_BUILDROOT) O=$(NT98513_OUTPUT) BR2_EXTERNAL=$(NT98513_EXTERNAL) all

# Package completed outputs only; do not rebuild, load or flash anything.
NT98513_RAM_TIMESTAMP ?= $(error Set NT98513_RAM_TIMESTAMP to a uint32 Unix timestamp)
NT98513_RAM_STAGE ?= $(error Set NT98513_RAM_STAGE to a candidate staging address)
NT98513_RAM_IMAGE ?= $(NT98513_OUTPUT)/images/nt98513-ram.uImage
.PHONY: nt98513-ram-image
nt98513-ram-image:
	@python3 "$(NT98513_EXTERNAL)/tools/make_ram_kernel_image.py" \
		--elf "$(NT98513_OUTPUT)/build/linux-custom/vmlinux" \
		--raw "$(NT98513_OUTPUT)/images/vmlinux.bin" \
		--timestamp "$(NT98513_RAM_TIMESTAMP)" \
		--staging-address "$(NT98513_RAM_STAGE)" --output "$(NT98513_RAM_IMAGE)"

# The optional consumer has its own output and explicit, offline source cache.
.PHONY: nt98513-owned-input-prepare-source nt98513-owned-input-check-source
.PHONY: nt98513-owned-input-defconfig nt98513-owned-input-source nt98513-owned-input
nt98513-owned-input-prepare-source:
	python3 "$(NT98513_EXTERNAL)/tools/prepare_divinus_source.py" \
		--checkout "$(NT98513_DIVINUS_CHECKOUT)" --output-dir "$(NT98513_DIVINUS_SOURCE_DIR)"

nt98513-owned-input-check-source:
	python3 "$(NT98513_EXTERNAL)/tools/prepare_divinus_source.py" \
		--verify-archive --output-dir "$(NT98513_DIVINUS_SOURCE_DIR)" --build-output "$(NT98513_OUTPUT)"

nt98513-owned-input-defconfig: nt98513-owned-input-check-source
	$(MAKE) -C "$(NT98513_BUILDROOT)" O="$(NT98513_OUTPUT)" BR2_EXTERNAL="$(NT98513_EXTERNAL)" \
		NT98513_DIVINUS_SOURCE_DIR="$(NT98513_DIVINUS_SOURCE_DIR)" nt98513_owned_input_defconfig

nt98513-owned-input-source: nt98513-owned-input-defconfig
	$(MAKE) -C "$(NT98513_BUILDROOT)" O="$(NT98513_OUTPUT)" BR2_EXTERNAL="$(NT98513_EXTERNAL)" \
		NT98513_DIVINUS_SOURCE_DIR="$(NT98513_DIVINUS_SOURCE_DIR)" source

nt98513-owned-input: nt98513-owned-input-defconfig
	$(MAKE) -C "$(NT98513_BUILDROOT)" O="$(NT98513_OUTPUT)" BR2_EXTERNAL="$(NT98513_EXTERNAL)" \
		NT98513_DIVINUS_SOURCE_DIR="$(NT98513_DIVINUS_SOURCE_DIR)" all
