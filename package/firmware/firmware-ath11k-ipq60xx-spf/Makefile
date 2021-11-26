include $(TOPDIR)/rules.mk

ifeq ($(DUMP)$(PKG_VERSION),)
  PKG_REV:=$(shell git ls-remote $(PKG_SOURCE_URL) $(PKG_BRANCH) | cut -b -7)
  PKG_VERSION:=g$(PKG_REV)
endif

PKG_NAME:=firmware-ath11k-ipq60xx-spf
PKG_RELEASE:=1

include $(INCLUDE_DIR)/package.mk

define Package/ath11k-firmware-default-ipq60xx
  SECTION:=firmware
  CATEGORY:=Firmware
  URL:="https://github.com/quic"
  MAINTAINER:=Ailick <277498654@qq.com>
  DEPENDS:=
endef

define Package/ath11k-firmware-ipq60xx-spf
$(Package/ath11k-firmware-default-ipq60xx)
  TITLE:=ath11k firmware for IPQ60xx devices
  VERSION:=WLAN.HK.2.5.0.1-01192-QCAHKSWPL_SILICONZ
endef

define Package/ath11k-firmware-ipq60xx-spf/description
Standard ath11k firmware for IPQ60xx from QCA
endef

define Package/qca-nss-fw-ipq60xx
$(Package/ath11k-firmware-default-ipq60xx)
  TITLE:=NSS firmware for IPQ60xx devices
  VERSION:=NSS.CP.11.4.0.5-5-R
endef

define Package/qca-nss-fw-ipq60xx/description
QCA_Networking_2021.SPF_11.4 firmware for IPQ60xx from QCA
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	cp -af $(TOPDIR)/qca/src/ath11k-bdf/IPQ6018/hw1.0/WLAN.HK.2.5.0.1.r5/WLAN.HK.2.5.0.1.r5-00004-QCAHKSWPL_SILICONZ-1/board-2.bin $(PKG_BUILD_DIR)/board-2.bin
	cp -af $(TOPDIR)/qca/src/qca-sdk-nss-fw/QCA_Networking_2021.SPF_11.4/CS/IPQ6018.ATH.11.4/retail_router0.bin $(PKG_BUILD_DIR)/retail_router0.bin
	cp -ar $(TOPDIR)/qca/src/upstream-wifi-fw/ath11k-firmware/IPQ6018/hw1.0/2.5.0.1/WLAN.HK.2.5.0.1-01192-QCAHKSWPL_SILICONZ-1 $(PKG_BUILD_DIR)/IPQ6018
endef

define Build/Compile

endef

define Package/ath11k-firmware-ipq60xx-spf/install
	$(INSTALL_DIR) $(1)/lib/firmware/IPQ6018
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/IPQ6018/* \
		$(1)/lib/firmware/IPQ6018
	$(INSTALL_DIR) $(1)/lib/firmware/ath11k/IPQ6018/hw1.0
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/board-2.bin \
		$(1)/lib/firmware/ath11k/IPQ6018/hw1.0/board-2.bin
	$(INSTALL_DATA) ./files/AX18/* \
		$(1)/lib/firmware/IPQ6018
#	$(INSTALL_DATA) ./files/qdss_trace_config.bin \
#		$(1)/lib/firmware/ath11k/IPQ6018/hw1.0/qdss_trace_config.bin
endef

define Package/qca-nss-fw-ipq60xx/install
	$(INSTALL_DIR) $(1)/lib/firmware/
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/retail_router0.bin \
		$(1)/lib/firmware/qca-nss0-retail.bin
endef

$(eval $(call BuildPackage,ath11k-firmware-ipq60xx-spf))
$(eval $(call BuildPackage,qca-nss-fw-ipq60xx))
