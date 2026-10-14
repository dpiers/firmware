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
