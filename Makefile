# mounthide - KernelSU module mount hide LKM
# 构建: make -C <kernel tree> M=$PWD ARCH=arm64 LLVM=1 ...
obj-m += mounthide.o

KERNELDIR ?= /home/tees/Tesla_Kernel/build/repo/common
CROSS ?= aarch64-linux-gnu-

all:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) ARCH=arm64 LLVM=1 \
		CROSS_COMPILE=$(CROSS) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) ARCH=arm64 LLVM=1 \
		CROSS_COMPILE=$(CROSS) clean

.PHONY: all clean
