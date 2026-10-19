# Isolated from OpenIPC's shared supplicant and Buildroot's 2.10 package.
WPA_SUPPLICANT_NT98513_VERSION = 2.12
WPA_SUPPLICANT_NT98513_SOURCE = wpa_supplicant-$(WPA_SUPPLICANT_NT98513_VERSION).tar.gz
WPA_SUPPLICANT_NT98513_SITE = https://w1.fi/releases
WPA_SUPPLICANT_NT98513_LICENSE = BSD-3-Clause
WPA_SUPPLICANT_NT98513_LICENSE_FILES = README
WPA_SUPPLICANT_NT98513_DEPENDENCIES = host-pkgconf libnl
# Environment variables permit upstream += additions; command-line LIBS would not.
WPA_SUPPLICANT_NT98513_LIBS = -lnl-3 -lm -lpthread

define WPA_SUPPLICANT_NT98513_CONFIGURE_CMDS
	$(INSTALL) -m 0644 $(WPA_SUPPLICANT_NT98513_PKGDIR)/supplicant.config $(@D)/wpa_supplicant/.config
endef

define WPA_SUPPLICANT_NT98513_BUILD_CMDS
	$(TARGET_MAKE_ENV) CFLAGS="$(TARGET_CFLAGS) -I$(STAGING_DIR)/usr/include/libnl3" \
		LDFLAGS="$(TARGET_LDFLAGS) -static" BINDIR=/usr/sbin \
		LIBS="$(WPA_SUPPLICANT_NT98513_LIBS)" LIBS_c="$(WPA_SUPPLICANT_NT98513_LIBS)" \
		$(MAKE) CC="$(TARGET_CC)" -C $(@D)/wpa_supplicant wpa_supplicant wpa_cli
endef

define WPA_SUPPLICANT_NT98513_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/wpa_supplicant/wpa_supplicant $(TARGET_DIR)/usr/sbin/wpa_supplicant
	$(INSTALL) -D -m 0755 $(@D)/wpa_supplicant/wpa_cli $(TARGET_DIR)/usr/sbin/wpa_cli
	$(INSTALL) -D -m 0644 $(@D)/README $(TARGET_DIR)/usr/share/licenses/wpa-supplicant-nt98513/README
endef

$(eval $(generic-package))
