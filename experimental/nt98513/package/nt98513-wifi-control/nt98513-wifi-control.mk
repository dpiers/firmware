# Original source-only runtime, scoped to the manual experimental variant.
NT98513_WIFI_CONTROL_VERSION = 1
NT98513_WIFI_CONTROL_SITE = $(patsubst %/,%,$(NT98513_WIFI_CONTROL_PKGDIR))
NT98513_WIFI_CONTROL_SITE_METHOD = local
NT98513_WIFI_CONTROL_LICENSE = MIT
NT98513_WIFI_CONTROL_LICENSE_FILES = LICENSE
NT98513_WIFI_CONTROL_DEPENDENCIES = wpa-supplicant-nt98513 busybox

define NT98513_WIFI_CONTROL_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) -std=c11 -Wall -Wextra -Werror \
		$(@D)/src/*.c $(TARGET_LDFLAGS) -static -o $(@D)/nt98513-wifi
endef

define NT98513_WIFI_CONTROL_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/nt98513-wifi $(TARGET_DIR)/usr/sbin/nt98513-wifi
	$(INSTALL) -d $(TARGET_DIR)/usr/libexec
	ln -sf ../sbin/nt98513-wifi $(TARGET_DIR)/usr/libexec/nt98513-wifi-lease
	$(INSTALL) -D -m 0644 $(@D)/files/owner.conf.example $(TARGET_DIR)/usr/share/nt98513-wifi/owner.conf.example
	$(INSTALL) -D -m 0644 $(@D)/LICENSE $(TARGET_DIR)/usr/share/licenses/nt98513-wifi-control/LICENSE
	if ! test -e $(TARGET_DIR)/etc/resolv.conf && ! test -L $(TARGET_DIR)/etc/resolv.conf; then \
		$(INSTALL) -D -m 0644 /dev/null $(TARGET_DIR)/etc/resolv.conf; \
	fi
endef

$(eval $(generic-package))
