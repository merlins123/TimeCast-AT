
ifeq ($(CPU),cc26x2_cc13x2)
  TI_VENDOR := vendor/cc13x2ware
  TI_DEVICE := $(TI_VENDOR)/ti/devices/cc13x2_cc26x2

  INCLUDES += -I$(CURDIR)/$(TI_VENDOR)
  CFLAGS += -DDeviceFamily_CC13X2

  SRC = $(TI_DEVICE)/driverlib/rfc.c
  SRC += $(TI_DEVICE)/rf_patches/rf_patch_cpe_bt5.c
  SRC += $(TI_VENDOR)/RFQueue/RFQueue.c
endif

include $(RIOTBASE)/Makefile.base

ifeq ($(CPU_MODEL),cc1352r1)
$(BINDIR)/$(MODULE)/$(TI_DEVICE)/driverlib/rfc.o: CFLAGS += -Wno-error=unused-parameter

$(BINDIR)/$(MODULE)/$(TI_VENDOR)/RFQueue/RFQueue.o: CFLAGS += \
  -Wno-error=strict-prototypes \
  -Wno-error=old-style-definition \
  -Wno-error=cast-align
endif
