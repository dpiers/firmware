include $(sort $(wildcard $(BR2_EXTERNAL_NT98513_PATH)/package/*/*.mk))

# The explicit fragment requests this variant; normalization must not change it.
ifneq ($(filter %/shared-clock.config,$(call qstrip,$(BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES))),)
define NT98513_CHECK_SHARED_CLOCK_KERNEL
	sh "$(BR2_EXTERNAL_NT98513_PATH)/tools/check_shared_clock_kernel.sh" "$(@D)"
endef
LINUX_POST_CONFIGURE_HOOKS += NT98513_CHECK_SHARED_CLOCK_KERNEL
LINUX_PRE_BUILD_HOOKS += NT98513_CHECK_SHARED_CLOCK_KERNEL
endif
# Optional offline Ethernet variant; existing variants above remain unchanged.
ifneq ($(filter %/ethernet.config,$(call qstrip,$(BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES))),)
define NT98513_CHECK_ETHERNET_KERNEL
	sh "$(BR2_EXTERNAL_NT98513_PATH)/tools/check_ethernet_kernel.sh" "$(@D)"
endef
LINUX_POST_CONFIGURE_HOOKS += NT98513_CHECK_ETHERNET_KERNEL
LINUX_PRE_BUILD_HOOKS += NT98513_CHECK_ETHERNET_KERNEL
endif

# USB remains disabled in DT; require the selected host implementation to build.
ifneq ($(filter %/usb-host.config,$(call qstrip,$(BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES))),)
define NT98513_CHECK_USB_HOST_KERNEL
	sh "$(BR2_EXTERNAL_NT98513_PATH)/tools/check_usb_host_kernel.sh" "$(@D)"
endef
LINUX_POST_CONFIGURE_HOOKS += NT98513_CHECK_USB_HOST_KERNEL
LINUX_PRE_BUILD_HOOKS += NT98513_CHECK_USB_HOST_KERNEL
endif
