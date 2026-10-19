RTL8188FU_FIRMWARE_NT98513_VERSION = 20240115
RTL8188FU_FIRMWARE_NT98513_SOURCE = linux-firmware-$(RTL8188FU_FIRMWARE_NT98513_VERSION).tar.xz
RTL8188FU_FIRMWARE_NT98513_SITE = $(BR2_KERNEL_MIRROR)/linux/kernel/firmware
RTL8188FU_FIRMWARE_NT98513_LICENSE = rtlwifi-firmware
RTL8188FU_FIRMWARE_NT98513_LICENSE_FILES = LICENCE.rtlwifi_firmware.txt WHENCE

# Literal audited members only: never expand or extract the full firmware tree.
define RTL8188FU_FIRMWARE_NT98513_EXTRACT_CMDS
	$(XZCAT) $(RTL8188FU_FIRMWARE_NT98513_DL_DIR)/$(RTL8188FU_FIRMWARE_NT98513_SOURCE) | \
		$(TAR) -C $(@D) --strip-components=1 --no-same-owner -xf - \
		linux-firmware-20240115/rtlwifi/rtl8188fufw.bin \
		linux-firmware-20240115/LICENCE.rtlwifi_firmware.txt \
		linux-firmware-20240115/WHENCE
	$(RTL8188FU_FIRMWARE_NT98513_VERIFY_FILES)
endef

# Recheck before installation as well as after extraction. The full source
# archive remains hash-checked and retained by Buildroot download/legal-info.
define RTL8188FU_FIRMWARE_NT98513_VERIFY_FILES
	test -d $(@D)/rtlwifi && test ! -L $(@D)/rtlwifi
	for f in rtlwifi/rtl8188fufw.bin LICENCE.rtlwifi_firmware.txt WHENCE; do \
		test -f $(@D)/$$f && test ! -L $(@D)/$$f || exit 1; \
	done
	cd $(@D) && printf '%s\n' \
		'e6189d6d7032b83f0ed4284629c43db66262449a866f33fd329e611239f62e1f  rtlwifi/rtl8188fufw.bin' \
		'a61351665b4f264f6c631364f85b907d8f8f41f8b369533ef4021765f9f3b62e  LICENCE.rtlwifi_firmware.txt' \
		'92d477fe15d81875bc5bcc4c54df7e95b65f42e4c44d64d840f39cd0aa153d6f  WHENCE' | sha256sum -c -
endef

define RTL8188FU_FIRMWARE_NT98513_INSTALL_TARGET_CMDS
	$(RTL8188FU_FIRMWARE_NT98513_VERIFY_FILES)
	$(INSTALL) -D -m 0644 $(@D)/rtlwifi/rtl8188fufw.bin $(TARGET_DIR)/lib/firmware/rtlwifi/rtl8188fufw.bin
	$(INSTALL) -D -m 0644 $(@D)/LICENCE.rtlwifi_firmware.txt $(TARGET_DIR)/usr/share/licenses/rtl8188fu-firmware-nt98513/LICENCE.rtlwifi_firmware.txt
	$(INSTALL) -D -m 0644 $(@D)/WHENCE $(TARGET_DIR)/usr/share/licenses/rtl8188fu-firmware-nt98513/WHENCE
endef

$(eval $(generic-package))
