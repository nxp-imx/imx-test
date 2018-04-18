#!/bin/sh

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

#mount none /sys/kernel/config/ -t configfs

id=1;

for udc_name in $(ls $UDC_DIR)
do
echo "WCID test is enabling udc:$udc_name"

mkdir /sys/kernel/config/usb_gadget/g$id
cd /sys/kernel/config/usb_gadget/g$id

echo 0x15a3 > idVendor
echo 0x0064 > idProduct

mkdir strings/0x409
echo 123456ABCDEF > strings/0x409/serialnumber
echo NXP > strings/0x409/manufacturer
echo "WINUSB" > strings/0x409/product

mkdir configs/c.1
mkdir configs/c.1/strings/0x409
echo Conf 1 > configs/c.1/strings/0x409/configuration

echo 5 > configs/c.1/MaxPower
echo 0xc0 > configs/c.1/bmAttributes

echo 1 > os_desc/use
echo "MSFT100" > os_desc/qw_sign
echo 0x40 > os_desc/b_vendor_code

mkdir functions/rndis.usb0
ln -s functions/rndis.usb0 configs/c.1
ln -s configs/c.1 os_desc

cd functions
echo RNDIS > rndis.usb0/os_desc/interface.rndis/compatible_id
echo 5162001 > rndis.usb0/os_desc/interface.rndis/sub_compatible_id

mkdir rndis.usb0/os_desc/interface.rndis/Icons
echo 2 > rndis.usb0/os_desc/interface.rndis/Icons/type
echo "%SystemRoot%\system32\shell32.dll,-233" > rndis.usb0/os_desc/interface.rndis/Icons/data

mkdir rndis.usb0/os_desc/interface.rndis/Label
echo 1 > rndis.usb0/os_desc/interface.rndis/Label/type
echo "MFG Device" > rndis.usb0/os_desc/interface.rndis/Label/data
cd ..

echo $udc_name > UDC

let ++id;

done
