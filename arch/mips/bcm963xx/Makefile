#
# Makefile for generic Broadcom MIPS boards
#
# Copyright (C) 2004 Broadcom Corporation
#
ifeq ($(strip $(CONFIG_BRCM_IKOS)),)
obj-y           := irq.o prom.o setup.o
else
obj-y           := irq.o ikos_setup.o
endif
obj-$(CONFIG_SMP)	+= smp-brcm.o

ifneq ($(strip $(CONFIG_BCM_HOSTMIPS_PWRSAVE)),)
obj-y += pwrmngtclk.o        
else
ifneq ($(strip $(CONFIG_BCM_DDR_SELF_REFRESH_PWRSAVE)),)
obj-y += pwrmngtclk.o        
endif
endif

obj-y += ktools/

SRCBASE         := $(TOPDIR)
EXTRA_CFLAGS    += -I$(INC_BRCMBOARDPARMS_PATH)/$(BRCM_BOARD) -I$(SRCBASE)/include -I$(INC_BRCMDRIVER_PUB_PATH)/$(BRCM_BOARD) -I$(INC_BRCMSHARED_PUB_PATH)/$(BRCM_BOARD) -I$(INC_BRCMSHARED_PUB_PATH)/pmc
#EXTRA_CFLAGS    += -I$(INC_ADSLDRV_PATH) -DDBG
EXTRA_CFLAGS    += -I$(INC_ADSLDRV_PATH) 
EXTRA_CFLAGS += -g
EXTRA_CFLAGS += $(BRCM_WERROR_CFLAGS)

## 20130822, TimLiu Add ZyXEL compile flags.
ifeq ($(strip ${ZYXEL}),y)
EXTRA_CFLAGS += -DZYXEL=1
ifneq ("$(strip ${ZYXEL_PRODUCT})","")
EXTRA_CFLAGS += -DZYPRJ_$(ZYXEL_PRODUCT)=1
endif
ifeq ($(strip ${ZYXEL_PARTITION}),y)
EXTRA_CFLAGS += -DZYXEL_PARTITION=1
endif
ifneq ($(strip ${ZYPRJ_HARDWARE_VERSION}),)
EXTRA_CFLAGS += -DZYPRJ_HARDWARE_VERSION=$(ZYPRJ_HARDWARE_VERSION)
endif
ifeq ($(strip ${ZYXEL_EXTERNAL_DECT}),y)
EXTRA_CFLAGS += -DZYXEL_EXTERNAL_DECT=1
endif
ifneq ($(strip ${ZYXEL_WIFI_LED_PATTERN}),)
EXTRA_CFLAGS += -DZYXEL_WIFI_LED_PATTERN=$(ZYXEL_WIFI_LED_PATTERN)
endif
else
EXTRA_CFLAGS += -DZYXEL=0
endif

ifneq ($(strip $(BUILD_SWMDK)),)
EXTRA_CFLAGS += -DSUPPORT_SWMDK
endif


ifeq "$(ADSL)" "ANNEX_B"
EXTRA_CFLAGS += -DADSL_ANNEXB
endif
ifeq "$(ADSL)" "SADSL"
EXTRA_CFLAGS += -DADSL_SADSL
endif
ifeq "$(ADSL)" "ANNEX_C"
EXTRA_CFLAGS += -DADSL_ANNEXC
endif
ifeq "$(BRCM_PHY_BONDING)" "y"
EXTRA_CFLAGS += -DSUPPORT_DSL_BONDING
endif
ifeq "$(BRCM_PHY_BONDING5B)" "y"
EXTRA_CFLAGS += -DSUPPORT_DSL_BONDING5B
endif

