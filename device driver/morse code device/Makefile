ifneq ($(KERNELRELEASE),)
# Follow this convention to add additional modules
	obj-m += morsedev.o
else
	KERNELDIR := /lib/modules/$(shell uname -r)/build
	PWD := $(shell pwd)
	ARCH := arm
	CROSS := arm-linux-
# To add multi source kernel modules
# obj-m += src1src2.o
# src1src2-objs := src1.o src2.o

all:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

endif
