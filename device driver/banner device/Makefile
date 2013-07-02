# Follow this convention to add additional modules
obj-m += bannerdev.o
# To add multi source kernel modules
# obj-m += src1src2.o
# src1src2-objs := src1.o src2.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
