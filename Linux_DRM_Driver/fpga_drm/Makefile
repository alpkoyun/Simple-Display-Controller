KERNELRELEASE ?=

ifneq ($(KERNELRELEASE),)

ccflags-y += -I$(src)/../XDMA_driver/include
ccflags-y += -I$(src)/../XDMA_driver/xdma

fpga_drm-y := fpga_drm_drv.o
fpga_drm-y += libxdma.o
fpga_drm-y += xdma_thread.o

obj-m := fpga_drm.o

else

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

.PHONY: all clean

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

endif
