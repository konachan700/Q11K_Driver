obj-m := q11k_device.o
KVERSION := $(shell uname -r)
KDIR := /lib/modules/$(KVERSION)/build
PWD := $(shell pwd)
modules modules_install clean:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) $@
install: modules_install
	install -D -m 0644 99-q11k_device.conf /etc/modprobe.d/99-q11k_device.conf
	depmod -a
uninstall:
	rm -vf /lib/modules/*/extra/q11k_device.o
	rm -vf /etc/modprobe.d/99-q11k_device.conf
	depmod -a
 
