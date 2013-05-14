#!/usr/bin/env bash

# Test script for ramdisk

# Pass in a,b,c,d for ramdisk name
# Must be run as root

modprobe osuramdisk

fdisk -l

echo "Test file" > tst.txt

dd if=tst.txt of=/dev/osuramdisk

cat /dev/osuramdisk

dmesg | grep "ram"

cd /dev

cfdisk /dev/osuramdisk$1

mkfs.ext2 /dev/osuramdisk$1

mkdir /media/ramdisk

mount /dev/osuramdisk1 /media/ramdisk

cp tst.txt /media/ramdisk

echo /media/ramdisk/tst.txt

dmesg | grep "ram"

