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

# Third, console-based experimental image. Use a new dedicated output.
.PHONY: nt98513-shared-clock-defconfig nt98513-shared-clock-source nt98513-shared-clock
nt98513-shared-clock-defconfig:
	python3 "$(NT98513_EXTERNAL)/tools/check_shared_clock_output.py" "$(NT98513_OUTPUT)"
	$(MAKE) -C "$(NT98513_BUILDROOT)" O="$(NT98513_OUTPUT)" BR2_EXTERNAL="$(NT98513_EXTERNAL)" nt98513_shared_clock_defconfig
nt98513-shared-clock-source: nt98513-shared-clock-defconfig
	$(MAKE) -C "$(NT98513_BUILDROOT)" O="$(NT98513_OUTPUT)" BR2_EXTERNAL="$(NT98513_EXTERNAL)" source
nt98513-shared-clock: nt98513-shared-clock-defconfig
	$(MAKE) -C "$(NT98513_BUILDROOT)" O="$(NT98513_OUTPUT)" BR2_EXTERNAL="$(NT98513_EXTERNAL)" all
# Optional offline Ethernet variant; existing variants above remain unchanged.
.PHONY: nt98513-ethernet-defconfig nt98513-ethernet-source nt98513-ethernet
nt98513-ethernet-defconfig:
	python3 "$(NT98513_EXTERNAL)/tools/check_ethernet_output.py" "$(NT98513_OUTPUT)"
	$(MAKE) -C "$(NT98513_BUILDROOT)" O="$(NT98513_OUTPUT)" BR2_EXTERNAL="$(NT98513_EXTERNAL)" nt98513_ethernet_defconfig
nt98513-ethernet-source: nt98513-ethernet-defconfig
	$(MAKE) -C "$(NT98513_BUILDROOT)" O="$(NT98513_OUTPUT)" BR2_EXTERNAL="$(NT98513_EXTERNAL)" source
nt98513-ethernet: nt98513-ethernet-defconfig
	$(MAKE) -C "$(NT98513_BUILDROOT)" O="$(NT98513_OUTPUT)" BR2_EXTERNAL="$(NT98513_EXTERNAL)" all

# Combined offline variant; its source and output are verified before Make.
.PHONY: nt98513-ethernet-owned-input-check-source nt98513-ethernet-owned-input-defconfig
.PHONY: nt98513-ethernet-owned-input-source nt98513-ethernet-owned-input
nt98513-ethernet-owned-input-check-source:
	python3 "$(NT98513_EXTERNAL)/tools/prepare_divinus_source.py" \
		--verify-archive --output-dir "$(NT98513_DIVINUS_SOURCE_DIR)" \
		--build-output "$(NT98513_OUTPUT)" --build-variant ethernet-owned-input
nt98513-ethernet-owned-input-defconfig: nt98513-ethernet-owned-input-check-source
	$(MAKE) -C "$(NT98513_BUILDROOT)" O="$(NT98513_OUTPUT)" BR2_EXTERNAL="$(NT98513_EXTERNAL)" \
		NT98513_DIVINUS_SOURCE_DIR="$(NT98513_DIVINUS_SOURCE_DIR)" nt98513_ethernet_owned_input_defconfig
nt98513-ethernet-owned-input-source: nt98513-ethernet-owned-input-defconfig
	$(MAKE) -C "$(NT98513_BUILDROOT)" O="$(NT98513_OUTPUT)" BR2_EXTERNAL="$(NT98513_EXTERNAL)" \
		NT98513_DIVINUS_SOURCE_DIR="$(NT98513_DIVINUS_SOURCE_DIR)" source
nt98513-ethernet-owned-input: nt98513-ethernet-owned-input-defconfig
	$(MAKE) -C "$(NT98513_BUILDROOT)" O="$(NT98513_OUTPUT)" BR2_EXTERNAL="$(NT98513_EXTERNAL)" \
		NT98513_DIVINUS_SOURCE_DIR="$(NT98513_DIVINUS_SOURCE_DIR)" all

# Explicit USB host candidate, preserving all prior output variants.
.PHONY: nt98513-usb-host-check-source nt98513-usb-host-defconfig
.PHONY: nt98513-usb-host-source nt98513-usb-host
nt98513-usb-host-check-source:
	python3 "$(NT98513_EXTERNAL)/tools/prepare_divinus_source.py" \
		--verify-archive --output-dir "$(NT98513_DIVINUS_SOURCE_DIR)" \
		--build-output "$(NT98513_OUTPUT)" --build-variant usb-host
nt98513-usb-host-defconfig: nt98513-usb-host-check-source
	$(MAKE) -C "$(NT98513_BUILDROOT)" O="$(NT98513_OUTPUT)" BR2_EXTERNAL="$(NT98513_EXTERNAL)" \
		NT98513_DIVINUS_SOURCE_DIR="$(NT98513_DIVINUS_SOURCE_DIR)" nt98513_usb_host_defconfig
nt98513-usb-host-source: nt98513-usb-host-defconfig
	$(MAKE) -C "$(NT98513_BUILDROOT)" O="$(NT98513_OUTPUT)" BR2_EXTERNAL="$(NT98513_EXTERNAL)" \
		NT98513_DIVINUS_SOURCE_DIR="$(NT98513_DIVINUS_SOURCE_DIR)" source
nt98513-usb-host: nt98513-usb-host-defconfig
	$(MAKE) -C "$(NT98513_BUILDROOT)" O="$(NT98513_OUTPUT)" BR2_EXTERNAL="$(NT98513_EXTERNAL)" \
		NT98513_DIVINUS_SOURCE_DIR="$(NT98513_DIVINUS_SOURCE_DIR)" all

# Manual-only Wi-Fi candidate. Every source check precedes Buildroot mutation.
.PHONY: nt98513-wifi-check-source nt98513-wifi-defconfig nt98513-wifi-source nt98513-wifi nt98513-wifi-image
nt98513-wifi-check-source:
	python3 "$(NT98513_EXTERNAL)/tools/prepare_divinus_source.py" \
		--verify-archive --output-dir "$(NT98513_DIVINUS_SOURCE_DIR)" \
		--build-output "$(NT98513_OUTPUT)" --build-variant wifi
nt98513-wifi-defconfig: nt98513-wifi-check-source
	$(MAKE) -C "$(NT98513_BUILDROOT)" O="$(NT98513_OUTPUT)" BR2_EXTERNAL="$(NT98513_EXTERNAL)" \
		NT98513_DIVINUS_SOURCE_DIR="$(NT98513_DIVINUS_SOURCE_DIR)" nt98513_wifi_defconfig
nt98513-wifi-source: nt98513-wifi-defconfig
	$(MAKE) -C "$(NT98513_BUILDROOT)" O="$(NT98513_OUTPUT)" BR2_EXTERNAL="$(NT98513_EXTERNAL)" \
		NT98513_DIVINUS_SOURCE_DIR="$(NT98513_DIVINUS_SOURCE_DIR)" source
nt98513-wifi: nt98513-wifi-defconfig
	$(MAKE) -C "$(NT98513_BUILDROOT)" O="$(NT98513_OUTPUT)" BR2_EXTERNAL="$(NT98513_EXTERNAL)" \
		NT98513_DIVINUS_SOURCE_DIR="$(NT98513_DIVINUS_SOURCE_DIR)" all
# At this fixed stage, the existing RAM_END guard limits header+payload to 8MiB.
NT98513_WIFI_IMAGE ?= $(NT98513_OUTPUT)/images/nt98513-wifi.uImage
nt98513-wifi-image: nt98513-wifi-check-source
	python3 "$(NT98513_EXTERNAL)/tools/make_ram_kernel_image.py" \
		--elf "$(NT98513_OUTPUT)/build/linux-custom/vmlinux" \
		--raw "$(NT98513_OUTPUT)/images/vmlinux.bin" \
		--timestamp "$(NT98513_RAM_TIMESTAMP)" --staging-address 0x02c00000 \
		--output "$(NT98513_WIFI_IMAGE)"

# Attach a new prerequisite without rewriting the frozen legacy recipe prefix.
.PHONY: nt98513-ram-reject-wifi-output
nt98513-ram-defconfig: nt98513-ram-reject-wifi-output
nt98513-ram-reject-wifi-output:
	python3 "$(NT98513_EXTERNAL)/tools/prepare_divinus_source.py" --reject-wifi-output "$(NT98513_OUTPUT)"

# Default-disabled SD host candidate with its own output and preserved Wi-Fi.
.PHONY: nt98513-mmc-check-source nt98513-mmc-defconfig nt98513-mmc-source nt98513-mmc nt98513-mmc-image
nt98513-mmc-check-source:
	python3 "$(NT98513_EXTERNAL)/board/check-mmc-output.py" "$(NT98513_OUTPUT)"
	python3 "$(NT98513_EXTERNAL)/tools/prepare_divinus_source.py" \
		--verify-archive --output-dir "$(NT98513_DIVINUS_SOURCE_DIR)" \
		--build-output "$(NT98513_OUTPUT)" --build-variant mmc
nt98513-mmc-defconfig: nt98513-mmc-check-source
	$(MAKE) -C "$(NT98513_BUILDROOT)" O="$(NT98513_OUTPUT)" BR2_EXTERNAL="$(NT98513_EXTERNAL)" \
		NT98513_DIVINUS_SOURCE_DIR="$(NT98513_DIVINUS_SOURCE_DIR)" nt98513_mmc_defconfig
nt98513-mmc-source: nt98513-mmc-defconfig
	$(MAKE) -C "$(NT98513_BUILDROOT)" O="$(NT98513_OUTPUT)" BR2_EXTERNAL="$(NT98513_EXTERNAL)" \
		NT98513_DIVINUS_SOURCE_DIR="$(NT98513_DIVINUS_SOURCE_DIR)" source
nt98513-mmc: nt98513-mmc-defconfig
	$(MAKE) -C "$(NT98513_BUILDROOT)" O="$(NT98513_OUTPUT)" BR2_EXTERNAL="$(NT98513_EXTERNAL)" \
		NT98513_DIVINUS_SOURCE_DIR="$(NT98513_DIVINUS_SOURCE_DIR)" all
NT98513_MMC_IMAGE ?= $(NT98513_OUTPUT)/images/nt98513-mmc.uImage
nt98513-mmc-image: nt98513-mmc-check-source
	python3 "$(NT98513_EXTERNAL)/tools/make_ram_kernel_image.py" \
		--elf "$(NT98513_OUTPUT)/build/linux-custom/vmlinux" \
		--raw "$(NT98513_OUTPUT)/images/vmlinux.bin" \
		--timestamp "$(NT98513_RAM_TIMESTAMP)" --staging-address 0x02c00000 \
		--output "$(NT98513_MMC_IMAGE)"

# Keep the legacy generic packager from writing an MMC output as another image.
.PHONY: nt98513-ram-reject-mmc-output
nt98513-ram-defconfig nt98513-ram-image: nt98513-ram-reject-mmc-output
nt98513-ram-reject-mmc-output:
	python3 "$(NT98513_EXTERNAL)/board/check-mmc-output.py" --reject-mmc "$(NT98513_OUTPUT)"
