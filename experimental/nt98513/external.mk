include $(sort $(wildcard $(BR2_EXTERNAL_NT98513_PATH)/package/*/*.mk))

# The explicit fragment requests this variant; normalization must not change it.
ifneq ($(filter %/shared-clock.config,$(call qstrip,$(BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES))),)
define NT98513_CHECK_SHARED_CLOCK_KERNEL
	sh "$(BR2_EXTERNAL_NT98513_PATH)/tools/check_shared_clock_kernel.sh" "$(@D)"
endef
LINUX_POST_CONFIGURE_HOOKS += NT98513_CHECK_SHARED_CLOCK_KERNEL
LINUX_PRE_BUILD_HOOKS += NT98513_CHECK_SHARED_CLOCK_KERNEL
endif
