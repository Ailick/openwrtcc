DEVICE_VARS += RAS_BOARD RAS_ROOTFS_SIZE RAS_VERSION

define Device/Default
	PROFILES := Default
	KERNEL_DEPENDS = $$(wildcard $(DTS_DIR)/$$(DEVICE_DTS).dts)
	KERNEL_INITRAMFS_PREFIX := $$(IMG_PREFIX)-$(1)-initramfs
	KERNEL_PREFIX := $$(IMAGE_PREFIX)
	KERNEL_LOADADDR := 0x80208000
	SUPPORTED_DEVICES := $(subst _,$(comma),$(1))
	IMAGE/sysupgrade.bin = sysupgrade-tar | append-metadata
	IMAGE/sysupgrade.bin/squashfs :=
endef

define Device/FitImage
	KERNEL_SUFFIX := -fit-uImage.itb
	KERNEL = kernel-bin | gzip | fit gzip $$(DTS_DIR)/$$(DEVICE_DTS).dtb
	KERNEL_NAME := Image
endef

define Device/FitImageLzma
	KERNEL_SUFFIX := -fit-uImage.itb
	KERNEL = kernel-bin | lzma | fit lzma $$(DTS_DIR)/$$(DEVICE_DTS).dtb
	KERNEL_NAME := Image
endef

define Device/FitzImage
	KERNEL_SUFFIX := -fit-zImage.itb
	KERNEL = kernel-bin | fit none $$(DTS_DIR)/$$(DEVICE_DTS).dtb
	KERNEL_NAME := zImage
endef

define Device/UbiFit
	KERNEL_IN_UBI := 1
	IMAGES := nand-factory.ubi nand-sysupgrade.bin
	IMAGE/nand-factory.ubi := append-ubi
	IMAGE/nand-sysupgrade.bin := sysupgrade-tar | append-metadata
endef

define Device/DniImage
	KERNEL_SUFFIX := -fit-uImage.itb
	KERNEL = kernel-bin | gzip | fit gzip $$(DTS_DIR)/$$(DEVICE_DTS).dtb
	KERNEL_NAME := Image
	NETGEAR_BOARD_ID :=
	NETGEAR_HW_ID :=
	IMAGES := factory.img sysupgrade.bin
	IMAGE/factory.img := append-kernel | pad-offset 64k 64 | append-uImage-fakehdr filesystem | append-rootfs | pad-rootfs | netgear-dni
	IMAGE/sysupgrade.bin := append-kernel | pad-offset 64k 64 | append-uImage-fakehdr filesystem | append-rootfs | pad-rootfs | append-metadata
endef
DEVICE_VARS += NETGEAR_BOARD_ID NETGEAR_HW_ID

define Build/my-make-ras
	sh make-ras.sh \
		--board $(RAS_BOARD) \
		--version $(RAS_VERSION) \
		--kernel $(call param_get_default,kernel,$(1),$(IMAGE_KERNEL)) \
		--rootfssize $(RAS_ROOTFS_SIZE) \
		--rootfs $@ \
		$@.new
	@mv $@.new $@
endef

define Device/cmiot_ax18
	$(call Device/FitImage)
	$(call Device/UbiFit)
	DEVICE_VENDOR := CMIOT
	DEVICE_TITLE := CMIOT-AX18
	DEVICE_MODEL := AX18
	DEVICE_DTS := qcom-ipq6018-cp03-c1
	DEVICE_DTS_CONFIG := config@cp03-c1
ifeq ($(CONFIG_TARGET_ipq_ipq60xx_64),)
	KERNEL_LOADADDR := $(IPQ60XX_KERNEL_LOADADDR)
else
	KERNEL_LOADADDR := $(IPQ60XX_64_KERNEL_LOADADDR)
endif
	SOC := ipq6000
	BLOCKSIZE := 128k
	PAGESIZE := 2048
	DEVICE_PACKAGES :=  +ath11k-firmware-ipq60xx-spf +qca-nss-fw-ipq60xx
	IMAGE/sysupgrade.bin := append-kernel | pad-to $$$${KERNEL_SIZE} | append-rootfs | pad-rootfs | append-metadata
	IMAGES += nand-factory.bin
	IMAGE/nand-factory.bin := append-ubi | qsdk-ipq-factory-nand
endef
TARGET_DEVICES += cmiot_ax18
