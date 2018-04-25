#!/bin/sh

# This composite gadget include 3 functions:
# - MASS STORAGE
# - ACM
# - NCM

#
# Exit status is 0 for PASS, nonzero for FAIL
#
STATUS=0

# Check if there is udc available, if not, return fail
UDC_DIR=/sys/class/udc
if test "$(ls -A "$UDC_DIR")"; then
	echo "The available udc:"
	for entry in "$UDC_DIR"/*
	do
		echo "$entry"
	done
else
	STATUS=1
	echo "No udc available!"
	exit $STATUS;
fi

# Get the back file of mass stoarge test, which is shared
# by all UDC
while :
do
echo "Enter back file name for mass storage test (^C to quit):"
read -e back_file
if [ -r $back_file ]; then
echo "$back_file will be used for mass storage test"
break
else
echo "The input back file $back_file does not exist"
continue
fi
done

#mount none /sys/kernel/config/ -t configfs

id=1;

for udc_name in $(ls $UDC_DIR)
do

mkdir /sys/kernel/config/usb_gadget/g$id
cd /sys/kernel/config/usb_gadget/g$id

# Use NXP VID, i.MX8QXP PID
echo 0x1fc9 > idVendor
echo 0x12cf > idProduct

mkdir strings/0x409
echo 123456ABCDEF > strings/0x409/serialnumber
echo NXP > strings/0x409/manufacturer
echo "NXP iMX USB Composite Gadget" > strings/0x409/product

mkdir configs/c.1
mkdir configs/c.1/strings/0x409

echo 5 > configs/c.1/MaxPower
echo 0xc0 > configs/c.1/bmAttributes

mkdir functions/mass_storage.1
echo $back_file > functions/mass_storage.1/lun.0/file
ln -s functions/mass_storage.1 configs/c.1/

mkdir functions/acm.1
ln -s functions/acm.1 configs/c.1

mkdir functions/ncm.1
echo 20 > functions/ncm.1/qmult
ln -s functions/ncm.1 configs/c.1

echo $udc_name > UDC

let ++id;

done
