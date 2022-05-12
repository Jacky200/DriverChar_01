#Ubuntu内核目录
KERN_DIR := /lib/modules/$(shell uname -r)/build
#STM32MP157内核目录
#CROSS_COMILE :=arm-buildroot-linux-gnueabihf-
#KERN_DIR := /home/jacky/100ask_stm32mp157_pro-sdk/Linux-5.4

#防止出现污染内核的错误
CONFIG_MODULE_SIG=n
all:
	make -C $(KERN_DIR) M=`pwd` modules
	$(CROSS_COMILE)gcc -o globalmem_test app01.c
.PHONY:clean
clean:
	make -C $(KERN_DIR) M=`pwd` modules clean
	rm -rf modules.order globalmem_test

globalmem-y :=globalmem_chardev.o
obj-m +=globalmem.o