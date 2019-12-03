#!/bin/bash

cInd=""
aCmd="aplay -l"
aPeriph=":"

tCard="wm8960"
eline=`eval $aCmd | grep -i $tCard | grep -m 1 -i $aPeriph`
if [ -n "$eline" ]; then
cNo=`echo $eline | cut -d: -f1 | cut -d' ' -f2`
dNo=`echo $eline | cut -d: -f2 | tr -s " " " " | cut -d' ' -f5`
cInd="$cNo,$dNo"
sed -i "/pcm.dmix_44100/,+10s/hw:.,0/hw:$cInd/" /etc/asound.conf
sed -i "/pcm.!dsnoop_44100/,+10s/hw:.,0/hw:$cInd/" /etc/asound.conf
exit
fi


tCard="wm8962"
eline=`eval $aCmd | grep -i $tCard | grep -m 1 -i $aPeriph`
if [ -n "$eline" ]; then
cNo=`echo $eline | cut -d: -f1 | cut -d' ' -f2`
dNo=`echo $eline | cut -d: -f2 | tr -s " " " " | cut -d' ' -f5`
cInd="$cNo,$dNo"
sed -i "/pcm.dmix_44100/,+10s/hw:.,0/hw:$cInd/" /etc/asound.conf
sed -i "/pcm.!dsnoop_44100/,+10s/hw:.,0/hw:$cInd/" /etc/asound.conf
exit
fi


tCard="wm8524"
eline=`eval $aCmd | grep -i $tCard | grep -m 1 -i $aPeriph`
if [ -n "$eline" ]; then
cNo=`echo $eline | cut -d: -f1 | cut -d' ' -f2`
dNo=`echo $eline | cut -d: -f2 | tr -s " " " " | cut -d' ' -f5`
cInd="$cNo,$dNo"
sed -i "/pcm.dmix_44100/,+10s/hw:.,0/hw:$cInd/" /etc/asound.conf
sed -i "/pcm.!dsnoop_44100/,+10s/hw:.,0/hw:$cInd/" /etc/asound.conf
exit
fi


tCard="cs42888"
eline=`eval $aCmd | grep -i $tCard | grep -m 1 -i $aPeriph`
if [ -n "$eline" ]; then
cNo=`echo $eline | cut -d: -f1 | cut -d' ' -f2`
dNo=`echo $eline | cut -d: -f2 | tr -s " " " " | cut -d' ' -f5`
cInd="$cNo,$dNo"
sed -i "/pcm.dmix_44100/,+10s/hw:.,0/hw:$cInd/" /etc/asound.conf
sed -i "/pcm.!dsnoop_44100/,+10s/hw:.,0/hw:$cInd/" /etc/asound.conf
exit
fi

