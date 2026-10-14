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
