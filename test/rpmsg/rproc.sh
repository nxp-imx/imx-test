#!/bin/sh

dmesg -c
dmesg -n 8
type=${2}
example=${3}

case ${1} in
	imx8mmevk)
		firmware1=imx8mm_m4_${type}_rpmsg_lite_pingpong_rtos_linux_remote.elf
		firmware2=imx8mm_m4_${type}_rpmsg_lite_str_echo_rtos.elf
		path=`find /sys/devices/platform/imx8mm-cm4 -name firmware`
		path=${path%?????????}
	;;
	imx8mnevk)
		firmware1=imx8mn_m7_${type}_rpmsg_lite_pingpong_rtos_linux_remote.elf
		firmware2=imx8mn_m7_${type}_rpmsg_lite_str_echo_rtos.elf
		path=`find /sys/devices/platform/imx8mn-cm7 -name firmware`
		path=${path%?????????}
	;;
	imx8mpevk)
		firmware1=imx8mp_m7_${type}_rpmsg_lite_pingpong_rtos_linux_remote.elf
		firmware2=imx8mp_m7_${type}_rpmsg_lite_str_echo_rtos.elf
		path=`find /sys/devices/platform/imx8mp-cm7 -name firmware`
		path=${path%?????????}
	;;
	imx8mqevk)
		firmware1=imx8mq_m4_${type}_rpmsg_lite_pingpong_rtos_linux_remote.elf
		firmware2=imx8mq_m4_${type}_rpmsg_lite_str_echo_rtos.elf
		path=`find /sys/devices/platform/imx8mq-cm4 -name firmware`
		path=${path%?????????}
	;;
	imx952evk)
		firmware1=imx952evk_m7_${type}_rpmsg_lite_pingpong_rtos_linux_remote.elf
		firmware2=imx952evk_m7_${type}_rpmsg_lite_str_echo_rtos.elf
		path=`find /sys/devices/platform/imx95-cm7 -name firmware`
		path=${path%?????????}
	;;
	imx95evk-19)
		firmware1=imx95-19x19-evk_m7_${type}_rpmsg_lite_pingpong_rtos_linux_remote.elf
		firmware2=imx95-19x19-evk_m7_${type}_rpmsg_lite_str_echo_rtos.elf
		path=`find /sys/devices/platform/imx95-cm7 -name firmware`
		path=${path%?????????}
	;;
	imx95verdin)
		firmware1=imx95-verdin-evk_m7_${type}_rpmsg_lite_pingpong_rtos_linux_remote.elf
		firmware2=imx95-verdin-evk_m7_${type}_rpmsg_lite_str_echo_rtos.elf
		path=`find /sys/devices/platform/imx95-cm7 -name firmware`
		path=${path%?????????}
	;;
	imx95evk-15)
		firmware1=imx95-15x15-evk_m7_${type}_rpmsg_lite_pingpong_rtos_linux_remote.elf
		firmware2=imx95-15x15-evk_m7_${type}_rpmsg_lite_str_echo_rtos.elf
		path=`find /sys/devices/platform/imx95-cm7 -name firmware`
		path=${path%?????????}
	;;
	imx95frdm-15)
		firmware1=imx95-15x15-frdm_m7_${type}_rpmsg_lite_pingpong_rtos_linux_remote.elf
		firmware2=imx95-15x15-frdm_m7_${type}_rpmsg_lite_str_echo_rtos.elf
		path=`find /sys/devices/platform/imx95-cm7 -name firmware`
		path=${path%?????????}
	;;
	imx93evk)
		firmware1=imx93-11x11-evk_m33_${type}_rpmsg_lite_pingpong_rtos_linux_remote.elf
		firmware2=imx93-11x11-evk_m33_${type}_rpmsg_lite_str_echo_rtos.elf
		path=/sys/devices/platform/remoteproc-cm33/remoteproc/remoteproc0
	;;
	imx93evkpf09)
		firmware1=imx93-11x11-evk_m33_${type}_rpmsg_lite_pingpong_rtos_linux_remote.elf
		firmware2=imx93-11x11-evk_m33_${type}_rpmsg_lite_str_echo_rtos.elf
		path=/sys/devices/platform/remoteproc-cm33/remoteproc/remoteproc0
	;;
	imx93qsb)
		firmware1=imx93-9x9-qsb_m33_${type}_rpmsg_lite_pingpong_rtos_linux_remote.elf
		firmware2=imx93-9x9-qsb_m33_${type}_rpmsg_lite_str_echo_rtos.elf
		path=/sys/devices/platform/remoteproc-cm33/remoteproc/remoteproc0
	;;
	imx943evk-m70)
		firmware1=imx943evk_cm7_core0_${type}_rpmsg_lite_pingpong_rtos_linux_remote.elf
		firmware2=imx943evk_cm7_core0_${type}_rpmsg_lite_str_echo_rtos.elf
		path=`find /sys/devices/platform/imx943-cm70 -name firmware`
		path=${path%?????????}
	;;
	imx943evk-m71)
		firmware1=imx943evk_cm7_core1_${type}_rpmsg_lite_pingpong_rtos_linux_remote.elf
		firmware2=imx943evk_cm7_core1_${type}_rpmsg_lite_str_echo_rtos.elf
		path=`find /sys/devices/platform/imx943-cm71 -name firmware`
		path=${path%?????????}
	;;
	imx943evk-m33s)
		firmware1=imx943evk_cm33_core1_${type}_rpmsg_lite_pingpong_rtos_linux_remote.elf
		firmware2=imx943evk_cm33_core1_${type}_rpmsg_lite_str_echo_rtos.elf
		path=`find /sys/devices/platform/imx943-cm33s -name firmware`
		path=${path%?????????}
	;;
	*)
	echo "Please input correct board"
	exit 1
esac

if [ ${3} == "tty" ]; then
modprobe imx_rpmsg_tty
else
modprobe imx_rpmsg_pingpong
fi

state=`cat ${path}/state`
if [ ${state} == "attached" ]; then
echo "Already started by bootloader, stop it"
echo stop > ${path}/state
fi

if [ ${3} == "tty" ]; then
rmmod imx_rpmsg_tty
else
rmmod imx_rpmsg_pingpong
fi

sleep 2
state=`cat ${path}/state`
if [ ${state} != "offline" ]; then
echo "not stopped"
exit 1
fi


count=0

while true
do
        count=$((${count}+1))
        if [ $count -gt 2 ]; then
                echo "Test Done"
                break;
        fi;

        dmesg -c > /dev/null
        echo "======${firmware1}==========="
        echo ${firmware1} > ${path}/firmware
        echo start > ${path}/state

        A=`dmesg | grep "imx_rproc_kick: failed"`
        if [ -z "${A}" ]; then
        echo "kick pass"
        else
        echo "kick failed"
        exit 1
        fi;

        modprobe imx_rpmsg_pingpong

        while true
        do
                A=`dmesg | grep "get 101"`
                if [ -z "${A}" ]; then
                echo "wait ping...."
                else
                dmesg -c > /dev/null
                break
                fi;
        done

	sleep 5
        echo stop > ${path}/state
        rmmod imx_rpmsg_pingpong

        sleep 2

        dmesg -c > /dev/null
	cat ${path}/state
        echo "======${firmware2}==========="
        echo ${firmware2} > ${path}/firmware
        echo start > ${path}/state

	sleep 1
        A=`dmesg | grep "imx_rproc_kick: failed"`
        if [ -z "${A}" ]; then
        echo "kick pass"
        else
        echo "kick failed"
        exit 1
        fi;

	sleep 5
        modprobe imx_rpmsg_tty

        while true
        do
                A=`dmesg | grep "hello world"`
                if [ -z "${A}" ]; then
                echo "wait tty...."
                else
                dmesg -c > /dev/null;
                break;
                fi;
        done

        echo stop > ${path}/state
        rmmod imx_rpmsg_tty

        sleep 2

done
