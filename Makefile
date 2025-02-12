obj-m	:= src/acer_wmi_rgb.o

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)

ccflags-y := -Dlts

# Sign the kernel module for secure boot (Ubuntu)
# https://wiki.ubuntu.com/UEFI/SecureBoot/Signing
# KEY := /var/lib/shim-signed/mok/MOK.priv
# X509 := /var/lib/shim-signed/mok/MOK.der

all: default

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) CONFIG_DEBUG_INFO_BTF_MODULES= modules

#   if [ -f "$(KEY)" ] && [ -f "$(X509)" ]; then
#	   kmodsign sha512 $(KEY) $(X509) acer_wmi_rgb.ko
#   fi

clean:
	rm -rf src/*.o src/*.mod src/*.mod.c src/.*.cmd .*.cmd modules.order Module.symvers

clean-all: clean
	rm -rf src/*.ko
