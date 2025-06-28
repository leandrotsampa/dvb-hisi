#===============================================================================
# export variables
#===============================================================================
ifeq ($(CFG_HI_EXPORT_FLAG),)
    ifneq ($(KERNELRELEASE),)
		KERNEL_DIR := $(srctree)
		SDK_DIR := $(shell cd $(KERNEL_DIR)/../../.. && /bin/pwd)
    else
		SDK_DIR := $(shell cd $(CURDIR)/../../../.. && /bin/pwd)
    endif

    ifeq ($(wildcard $(SDK_DIR)/base.mak),)
        ifneq ($(srctree),)
            KERNEL_DIR := $(srctree)
            SDK_DIR := $(shell cd $(KERNEL_DIR) && /bin/pwd)
        else
            SDK_DIR := $(shell cd $(CURDIR)/.. && /bin/pwd)
        endif
    endif

    include $(SDK_DIR)/base.mak
endif

#===============================================================================
# local variables
#===============================================================================
ifeq ($(CFG_HI_HDMI_SUPPORT_2_0),y)
HDMI_VER := hdmi_2_0
HDMI_DRV_DIR := $(MSP_DIR)/drv/hdmi/$(HDMI_VER)
LINUX_VERSION := hisiv200

ifeq ($(CFG_HI_CHIP_TYPE),hi3798cv200_a)
PRODUCT_DIR := HifoneB02
endif

ifeq ($(CFG_HI_CHIP_TYPE),hi3798cv200)
PRODUCT_DIR := Hi3798CV200
endif

ifneq ($(findstring $(CFG_HI_CHIP_TYPE), hifonev300 hifonev200 hi3798mv200 hi3798mv200_a),)
PRODUCT_DIR := hisiv100
endif

ifneq ($(findstring $(CFG_HI_CHIP_TYPE), hi3796mv200),)
PRODUCT_DIR := hi3796mv200
endif

EXTRA_CFLAGS += -I$(HDMI_DRV_DIR)
EXTRA_CFLAGS += -I$(HDMI_DRV_DIR)/hal
EXTRA_CFLAGS += -I$(HDMI_DRV_DIR)/hal/emi
EXTRA_CFLAGS += -I$(HDMI_DRV_DIR)/osal/$(LINUX_VERSION)
EXTRA_CFLAGS += -I$(HDMI_DRV_DIR)/product/$(PRODUCT_DIR)
endif

EXTRA_CFLAGS += $(CFG_HI_KMOD_CFLAGS)
EXTRA_CFLAGS += $(CFG_HI_BOARD_CONFIGS)
EXTRA_CFLAGS += -I$(COMMON_UNF_INCLUDE)                     \
                -I$(COMMON_API_INCLUDE)                     \
                -I$(COMMON_DRV_INCLUDE)                     \
                -I$(MSP_UNF_INCLUDE)                        \
                -I$(MSP_API_INCLUDE)                        \
                -I$(MSP_DRV_INCLUDE)                        \
                -I$(COMPONENT_DIR)/ha_codec/include         \
                -I$(MSP_DIR)/api/higo/include               \
                -I$(COMMON_DIR)/drv/sys                     \
                -I$(COMMON_DIR)/drv/mmz                     \
                -I$(MSP_DIR)/api/tde/include                \
                -I$(MSP_DIR)/drv/adsp/adsp_v1_1/include     \
				-I$(MSP_DIR)/api/hdmi                       \
                -I$(MSP_DIR)/drv/hdmi/$(HDMI_VER)           \
				-I$(MSP_DIR)/api/sio                        \
				-I$(COMMON_DRV_INCLUDE)/$(CFG_HI_CHIP_TYPE) \
				-I$(MSP_DIR)/api/ao

EXTRA_CFLAGS += -Idrivers/media/dvb-core		\
                -Idrivers/media/dvb/dvb-core	\
                -Idrivers/media/dvb/frontends	\
                -Idrivers/media/common/tuners	\
                -Iinclude                       \
                -I$(MSP_DIR)/drv/dvb-hisi

ifeq ($(CFG_HI_DEMOD_TYPE_GX1132),y)
EXTRA_CFLAGS += -DDEMOD_DEV_TYPE_GX1132
endif

ifeq ($(CFG_HI_DEMOD_TYPE_FC8300),y)
EXTRA_CFLAGS += -DDEMOD_DEV_TYPE_FC8300
endif

EXTRA_CFLAGS += $(CFLAGS) -DHI_CHIP_TYPE=\"$(CFG_HI_CHIP_TYPE)\" -DENIGMA2

KBUILD_EXTRA_SYMBOLS += $(COMMON_DIR)/drv/Module.symvers

MOD_NAME    := dvb-hisi
MOD_VERSION := $(shell echo "\#define MOD_VERSION \"$(shell date "+%Y.%m-%d (Build: %H%M%S)")\"">$(MSP_DIR)/drv/$(MOD_NAME)/module_version.h)

obj-$(HI_DRV_BUILDTYPE) += $(MOD_NAME).o

$(MOD_NAME)-y := box_detector.o \
                 ca.o \
                 dvb_hisi.o \
                 enigma2_proc.o \
                 frontend.o \
                 socket.o

REMOVED_FILES := "*.o" "*.ko" "*.order" "*.symvers" "*.mod" "*.mod.*" "*.cmd" ".tmp_versions" "module_version.h" "modules.builtin"

#===============================================================================
#   rules
#===============================================================================
.PHONY: all clean install uninstall

all:
	$(AT)make -C $(LINUX_DIR) ARCH=$(CFG_HI_CPU_ARCH) CROSS_COMPILE=$(HI_KERNEL_TOOLCHAINS_NAME)- M=$(CURDIR) modules

debug:
	$(AT)make -C $(LINUX_DIR) CFLAGS=-g ARCH=$(CFG_HI_CPU_ARCH) CROSS_COMPILE=$(HI_KERNEL_TOOLCHAINS_NAME)- M=$(CURDIR) modules

strip: all
	$(HI_KERNEL_TOOLCHAINS_NAME)-strip --strip-debug $(MOD_NAME).ko
	$(HI_KERNEL_TOOLCHAINS_NAME)-strip --strip-unneeded $(MOD_NAME).ko

clean:
	$(AT)find ./ -name "*.d" $(foreach file, $(REMOVED_FILES), -o -name $(file)) | xargs rm -rf

install:all
	$(AT)cp -f $(CURDIR)/$(MOD_NAME).ko $(HI_MODULE_DIR)/

uninstall:
	$(AT)rm -rf $(HI_MODULE_DIR)/$(MOD_NAME).ko