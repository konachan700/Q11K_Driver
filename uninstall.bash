#!/bin/bash

[ ! -z "$(lsmod | grep q11)" ] && rmmod q11k_device
rm -vf /lib/modules/*/extra/q11k_device.ko
rm -vf /etc/modprobe.d/99-q11k_device.conf
depmod -a