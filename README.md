# JIG.m2.self
ODROID-M2 (RK3588) Self-test Jig Source

### Documentation for JIG development
* Document : https://docs.google.com/spreadsheets/d/14jsR5Y7Cq3gO_OViS7bzTr-GcHdNRS_1258eUWI2aqs/edit#gid=1673179576

### Base linux image
 * http://192.168.0.224:8080/RK3588/ODROID-M2/Ubuntu/ubuntu-20.04-server-odroidm2-20240715.img.xz

### Update Image info (apt update && apt upgrade -y)
 * Kernel : Linux server 5.10.0-odroid-arm64 #1 SMP Ubuntu 5.10.198-202406031228~focal (2024-06-03) aarch64 aarch64 aarch64 GNU/Linux
 * U-Boot : U-Boot 2017.09-g288fbfedb12-231011 #tobetter (Jul 15 2024 - 16:16:55 +0900)

### SSH connect error (permission denied)
 * root@server:~# rm /etc/ssh/sshd_config.d/60-cloudimg-settings.conf
 * root@server:~# service ssh restart

### Self mode settings
* Install ubuntu package & python3 module
```
// odroid server repo add
root@server:~# echo "192.168.0.224 ppa.linuxfactory.or.kr" >> /etc/hosts
root@server:~# echo "deb  http://ppa.linuxfactory.or.kr focal internal" >> /etc/apt/sources.list.d/ppa-linuxfactory-or-kr.list

// ubuntu system update
root@server:~# apt update && apt upgrade -y

// ubuntu package
root@server:~# apt install samba ssh build-essential python3 python3-pip ethtool net-tools usbutils git i2c-tools vim cups cups-bsd overlayroot nmap 

// python3 package
root@server:~# pip install aiohttp asyncio

// git config
root@server:~# git config --global user.email "charles.park@hardkenrel.com"
root@server:~# git config --global user.name "charles-park"
root@server:~# git config --global core.editor "vim"

```

* Edit the /boot/config.ini
```
root@server:~# vi /boot/config.ini
```
```
[generic]
overlay_resize=16384
overlay_profile=""
# default
# overlays="uart0 i2c0 i2c1"

# modified
overlays=""

[overlay_custom]
overlays="i2c0 i2c1"

[overlay_hktft32]
overlays="hktft32"

[overlay_hktft35]
overlays="hktft35 sx865x-i2c1"
```
### Clone the reopsitory with submodule
```
root@odroid:~# git clone --recursive https://github.com/charles-park/JIG.m2.self

or

root@odroid:~# git clone https://github.com/charles-park/JIG.m2.self
root@odroid:~# cd JIG.m2.self
root@odroid:~/JIG.m2.self# git submodule update --init --recursive

// app build and install
root@odroid:~/JIG.m2.self# make
root@odroid:~/JIG.m2.self# cd service
root@odroid:~/JIG.m2.self/service# ./install

```

### iperf3 install
```
root@server:~# apt install iperf3
```

### add root user, ssh root enable (https://www.leafcats.com/176)
```
// root user add
root@server:~# passwd root

root@server:~# vi /etc/ssh/sshd_config

...
# PermitRootLogin prohibit-password
PermitRootLogin yes
StrictModes yes
PubkeyAuthentication yes
...

root@server:~# service sshd restart
```

### auto login
```
root@server:~# systemctl edit getty@tty1.service
```
```
[Service]
ExecStart=
ExecStart=-/sbin/agetty --noissue --autologin root %I $TERM
Type=idle
```

### samba config
```
root@server:~# smbpasswd -a root
root@server:~# vi /etc/samba/smb.conf
```
```
[odroidm2-self]
   comment = odroid-m1s jig root
   path = /root
   guest ok = no
   browseable = yes
   writable = yes
   create mask = 0775
   directory mask = 0775
```
```
// samba restart
root@server:~# service smbd restart
```

### Sound setup
```
// Codec info
root@server:~# aplay -l
**** List of PLAYBACK Hardware Devices ****
card 0: rockchipes8316c [rockchip,es8316-codec], device 0: fe470000.i2s-ES8316 HiFi ES8316 HiFi-0 [fe470000.i2s-ES]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
card 1: rockchipdp0 [rockchip-dp0], device 0: rockchip-dp0 spdif-hifi-0 [rockchip-dp0 spdif-hifi-0]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
card 2: rockchiphdmi0 [rockchip-hdmi0], device 0: rockchip-hdmi0 i2s-hifi-0 [rockchip-hdmi0 i2s-hifi-0]
  Subdevices: 1/1
  Subdevice #0: subdevice #0

// Mixer info
root@server:~# amixer -c 1
Simple mixer control 'Headphone',0
  Capabilities: pvolume
  Playback channels: Front Left - Front Right
  Limits: Playback 0 - 3
  Mono:
  Front Left: Playback 1 [33%] [-24.00dB]
  Front Right: Playback 1 [33%] [-24.00dB]
Simple mixer control 'Headphone Mixer',0
  Capabilities: volume
  Playback channels: Front Left - Front Right
  Capture channels: Front Left - Front Right
  Limits: 0 - 11
  Front Left: 0 [0%] [-12.00dB]
  Front Right: 0 [0%] [-12.00dB]

...

Simple mixer control 'Headphone',0
  Capabilities: pvolume
  Playback channels: Front Left - Front Right
  Limits: Playback 0 - 3
  Mono:
  Front Left: Playback 1 [33%] [-24.00dB]
  Front Right: Playback 1 [33%] [-24.00dB]
Simple mixer control 'Headphone Mixer',0
  Capabilities: volume
  Playback channels: Front Left - Front Right
  Capture channels: Front Left - Front Right
  Limits: 0 - 11
  Front Left: 0 [0%] [-12.00dB]
  Front Right: 0 [0%] [-12.00dB]

// config mixer
root@server:~# amixer sset 'Right Headphone Mixer Right DAC' on
root@server:~# amixer sset 'Left Headphone Mixer Left DAC' on
root@server:~# amixer sset 'Headphone' 3
root@server:~# amixer sset 'Headphone Mixer' 11
root@server:~# amixer sset 'DAC' 192
root@server:~# alsactl store
```

* Sound test (Sign-wave 1Khz)
```
// use speaker-test
root@server:~# speaker-test -D hw:0,0 -c 2 -t sine -f 1000           # pin header target, all
root@server:~# speaker-test -D hw:0,0 -c 2 -t sine -f 1000 -p 1 -s 1 # pin header target, left
root@server:~# speaker-test -D hw:0,0 -c 2 -t sine -f 1000 -p 1 -s 2 # pin header target, right

// or use aplay with (1Khz audio file)
root@server:~# aplay -Dhw:1,0 {audio file} -d {play time}
```

### Overlay root
* overlayroot enable
```
// update boot.scr
root@server:~# update-initramfs -c -k $(uname -r)
Using DTB: rockchip/rk3566-odroid-m1s.dtb
Installing rockchip into /boot/dtbs/5.10.0-odroid-arm64/rockchip/
Installing rockchip into /boot/dtbs/5.10.0-odroid-arm64/rockchip/
flash-kernel: installing version 5.10.0-odroid-arm64
Generating boot script u-boot image... done.
Taking backup of boot.scr.
Installing new boot.scr.

// update ramdisk
root@server:~# mkimage -A arm64 -O linux -T ramdisk -C none -a 0 -e 0 -n uInitrd -d /boot/initrd.img-$(uname -r) /boot/uInitrd 
Image Name:   uInitrd
Created:      Fri Oct 27 04:27:58 2023
Image Type:   AArch64 Linux RAMDisk Image (uncompressed)
Data Size:    7805996 Bytes = 7623.04 KiB = 7.44 MiB
Load Address: 00000000
Entry Point:  00000000

// Change overlayroot value "" to "tmpfs" for overlayroot enable
root@server:~# vi /etc/overlayroot.conf
...
overlayroot_cfgdisk="disabled"
overlayroot="tmpfs"
```
* overlayroot disable
```
// get write permission
root@server:~# overlayroot-chroot 
INFO: Chrooting into [/media/root-ro]
root@server:~# 

// Change overlayroot value "tmpfs" to "" for overlayroot disable
root@server:~# vi /etc/overlayroot.conf
```
