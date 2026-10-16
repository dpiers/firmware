# Source origin: https://github.com/dpiers/divinus, exact commit in source-manifest.json.
# This owner-fork experiment stages a hash-verified source-only archive offline.
ifeq ($(BR2_PACKAGE_DIVINUS_OWNED_NT98513),y)
ifeq ($(filter /%,$(NT98513_DIVINUS_SOURCE_DIR)),)
$(error Set NT98513_DIVINUS_SOURCE_DIR to the absolute prepared source cache)
endif
endif

DIVINUS_OWNED_NT98513_VERSION = cfc60e6dcd9fa79e13731b6f07537050209e28609b2d7b7cae7fbbb81da3c006
DIVINUS_OWNED_NT98513_SITE = file://$(NT98513_DIVINUS_SOURCE_DIR)
DIVINUS_OWNED_NT98513_SOURCE = divinus-owned-nt98513-$(DIVINUS_OWNED_NT98513_VERSION).tar.gz
DIVINUS_OWNED_NT98513_LICENSE = GPL-2.0-only, MIT, GPL-2.0 WITH Linux-syscall-note (UAPI)
DIVINUS_OWNED_NT98513_LICENSE_FILES = LICENSE COPYING licenses/GPL-2.0 licenses/Linux-syscall-note licenses/MIT-owned-rtcp
DIVINUS_OWNED_NT98513_DEPENDENCIES = host-python3

define DIVINUS_OWNED_NT98513_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/src divinus-owned \
		CC="$(TARGET_CC)" PYTHON="$(HOST_DIR)/bin/python3" \
		OWNED_DEPS="$(@D)/third_party/nt98513-owned-capture" OWNED_OUT="$(@D)/owned-build" \
		OWNED_OPT="$(TARGET_CFLAGS) $(TARGET_LDFLAGS) -static"
endef

define DIVINUS_OWNED_NT98513_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/owned-build/divinus-owned $(TARGET_DIR)/usr/bin/divinus-owned
	$(INSTALL) -D -m 0644 $(@D)/owned-input.conf.example $(TARGET_DIR)/usr/share/divinus-owned/owned-input.conf.example
	$(INSTALL) -D -m 0644 $(@D)/docs/owned-input.md $(TARGET_DIR)/usr/share/divinus-owned/APPLICATION.md
	$(INSTALL) -D -m 0644 $(@D)/PACKAGE.md $(TARGET_DIR)/usr/share/divinus-owned/README.md
	$(INSTALL) -D -m 0644 $(@D)/LICENSE $(TARGET_DIR)/usr/share/licenses/divinus-owned/MIT
	$(INSTALL) -D -m 0644 $(@D)/COPYING $(TARGET_DIR)/usr/share/licenses/divinus-owned/NOTICES
	$(INSTALL) -D -m 0644 $(@D)/licenses/GPL-2.0 $(TARGET_DIR)/usr/share/licenses/divinus-owned/GPL-2.0
	$(INSTALL) -D -m 0644 $(@D)/licenses/Linux-syscall-note $(TARGET_DIR)/usr/share/licenses/divinus-owned/Linux-syscall-note
	$(INSTALL) -D -m 0644 $(@D)/licenses/MIT-owned-rtcp $(TARGET_DIR)/usr/share/licenses/divinus-owned/MIT-owned-rtcp
endef

$(eval $(generic-package))
