#!/bin/bash

echo ""
echo ""
echo " * ODROID-JIG Kernel Update * "
echo ""
echo ""

echo "192.168.0.224 ppa.linuxfactory.or.kr" >> /etc/hosts
echo "deb  http://ppa.linuxfactory.or.kr focal internal" >> /etc/apt/sources.list.d/ppa-linuxfactory-or-kr.list

apt update && apt upgrade -y && sync

echo ""
echo ""
echo " * ODROID-JIG Ubuntu Package install * "
echo ""
echo ""

apt install samba ssh build-essential python3 python3-pip ethtool net-tools usbutils git i2c-tools vim cups cups-bsd overlayroot nmap iperf3 alsa-utils
pip install aiohttp asyncio

git config --global user.email "charles.park@hardkenrel.com"
git config --global user.name "charles-park"
git config --global core.editor "vim"
sync

echo ""
echo ""
echo " * ODROID-JIG sound setup * "
echo ""
echo ""
amixer sset 'Right Headphone Mixer Right DAC' on
amixer sset 'Left Headphone Mixer Left DAC' on
amixer sset 'Headphone' 3
amixer sset 'Headphone Mixer' 11
amixer sset 'DAC' 192
alsactl store && sync

echo ""
echo ""
echo " * ODROID-JIG ramdisk & config update * "
echo ""
echo ""

cp ./service/config.ini /boot/
cp -r /usr/share/flash-kernel/preboot.d/vendor /usr/share/flash-kernel/preboot.d/upstream
update-bootscript
sync

echo ""
echo ""
echo " * ODROID-JIG service install * "
echo ""
echo ""

make clean && make

systemctl disable odroid-jig.service && sync

cp ./service/odroid-jig.service /etc/systemd/system/ && sync

systemctl enable odroid-jig.service && sync

systemctl restart odroid-jig.service && sync

echo ""
echo ""
echo " * ODROID-JIG install end (ssh root enable, overlayroot, samba setup) * "
echo ""
echo "   // add user smb, system "
echo "   smbpasswd -a root, passwd root.... "
echo ""
echo "   // auto login setup "
echo "   root&server:~# systemctl edit getty@tty1.service "
echo "   [Service] "
echo "   ExecStart= "
echo "   ExecStart=-/sbin/agetty --noissue --autologin root %I $TERM "
echo "   Type=idle "
echo ""
echo "   // config overlay system "
echo "   root&server:~# vi /etc/overlayroot.conf "
echo ""
echo ""
cp ./service/sshd_config /etc/ssh/ && sync
# root@server:/etc/ssh/sshd_config.d# rm -f 60-cloudimg-settings.conf
rm -f /etc/ssh/sshd_config.d/60-cloudimg-settings.conf && sync
cp ./service/smb.conf /etc/samba/smb.conf && sync

