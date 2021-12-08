define KernelPackage/qrtr_mproc
  TITLE:= Ath11k Specific kernel configs for IPQ807x, IPQ60xx and IPQ50xx
  DEPENDS+= @TARGET_ipq_ipq807x||TARGET_ipq_ipq807x_64||TARGET_ipq_ipq60xx||TARGET_ipq_ipq60xx_64||TARGET_ipq_ipq50xx||TARGET_ipq_ipq50xx_64
  KCONFIG:= \
	  CONFIG_QRTR=y \
	  CONFIG_QCOM_APCS_IPC=y \
	  CONFIG_QCOM_GLINK_SSR=y \
	  CONFIG_QCOM_Q6V5_WCSS=y \
	  CONFIG_MSM_RPM_RPMSG=y \
	  CONFIG_RPMSG_QCOM_GLINK_RPM=y \
	  CONFIG_REGULATOR_RPM_GLINK=y \
	  CONFIG_QCOM_SYSMON=y \
	  CONFIG_RPMSG=y \
	  CONFIG_RPMSG_CHAR=y \
	  CONFIG_RPMSG_QCOM_GLINK_SMEM=y \
	  CONFIG_RPMSG_QCOM_SMD=y \
	  CONFIG_QRTR_SMD=y \
	  CONFIG_QCOM_QMI_HELPERS=y \
	  CONFIG_SAMPLES=y \
	  CONFIG_SAMPLE_QMI_CLIENT=m \
	  CONFIG_SAMPLE_TRACE_EVENTS=n \
	  CONFIG_SAMPLE_KOBJECT=n \
	  CONFIG_SAMPLE_KPROBES=n \
	  CONFIG_SAMPLE_KRETPROBES=n \
	  CONFIG_SAMPLE_HW_BREAKPOINT=n \
	  CONFIG_SAMPLE_KFIFO=n \
	  CONFIG_SAMPLE_CONFIGFS=n \
	  CONFIG_SAMPLE_RPMSG_CLIENT=n \
	  CONFIG_USB_GADGET=m \
	  CONFIG_USB_CONFIGFS=m \
	  CONFIG_USB_CONFIGFS_F_FS=y \
	  CONFIG_MAILBOX=y \
	  CONFIG_DIAG_OVER_QRTR=y
endef

define KernelPackage/qrtr_mproc/description
Kernel configs for ath11k support specific to ipq807x, IPQ60xx and IPQ50xx
endef

$(eval $(call KernelPackage,qrtr_mproc))

define KernelPackage/mhi-qrtr-mproc
  TITLE:= Default kernel configs for QCCI to work with QRTR.
  DEPENDS+= @TARGET_ipq_ipq807x||TARGET_ipq_ipq807x_64||TARGET_ipq_ipq60xx||TARGET_ipq_ipq60xx_64||TARGET_ipq_ipq50xx||TARGET_ipq_ipq50xx_64
  KCONFIG:= \
	  CONFIG_QRTR=y \
	  CONFIG_QRTR_MHI=y \
	  CONFIG_MHI_BUS=y \
	  CONFIG_MHI_QTI=y \
	  CONFIG_MHI_NETDEV=y \
	  CONFIG_MHI_DEBUG=y \
	  CONFIG_MHI_UCI=y
endef

define KernelPackage/mhi-qrtr-mproc/description
Default kernel configs for QCCI to work with QRTR.
endef

$(eval $(call KernelPackage,mhi-qrtr-mproc))
