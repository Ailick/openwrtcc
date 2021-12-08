ccflags-y += -I$(obj)/../exports -I$(obj)/.. -I$(obj)/nss_hal/include
ccflags-y += -DNSS_CLIENT_BUILD_ID="$(BUILD_ID)"
ccflags-y += -DNSS_VXLANMGR_DEBUG_LEVEL=1

CFLAGS_REMOVE_nss_vxlanmgr.o = -Wall -Werror
obj-m += qca-nss-vxlanmgr.o
qca-nss-vxlanmgr-objs := nss_vxlanmgr.o
qca-nss-vxlanmgr-objs += nss_vxlanmgr_tunnel.o
qca-nss-vxlanmgr-objs += nss_vxlanmgr_tun_stats.o
