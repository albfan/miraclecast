# MiracleCast - Wifi-Display/Miracast Implementation

The MiracleCast project provides software to connect external monitors to your system via Wi-Fi. It is compatible to the Wifi-Display specification also known as Miracast. MiracleCast implements the Display-Source as well as Display-Sink side.

The Display-Source side allows you to connect external displays to your system and stream local content to the device. A lot of effort is put into making this as easy as connecting external displays via HDMI.

On the other hand, the Display-Sink side allows you to create wifi-capable external displays yourself. You can use it on your embedded devices or even on full desktops to allow other systems to use your device as external display.


## Requirements

The MiracleCast projects requires the following software to be installed:
 - **systemd**: A system management daemon. It is used for device-management (udev), dbus management (sd-bus) and service management.
    Systemd must be compiled with --enable-kdbus, even though kdbus isn't used, but only the independent, experimental sd-libraries.
    *required*: >=systemd-213

 - **glib**: A utility library. Used by the current DHCP implementation. Will be removed once sd-dns gains DHCP-server capabilities.
    *required*: ~=glib2-2.38 (might work with older releases, untested..)

 - **check**: Test-suite for C programs. Used for optional tests of the MiracleCast code base.
    *optional*: ~=check-0.9.11 (might work with older releases, untested..)

 - **gstreamer**: MiracleCast relay on gstreamer to show cast its output. You can test if all needed is installed launching [res/test-viewer.sh](https://github.com/albfan/miraclecast/blob/master/res/test-viewer.sh)

 - **P2P Wi-Fi device** Although widespread this days, there are some devices not compatible with [Wi-Fi Direct](http://en.wikipedia.org/wiki/Wi-Fi_Direct) (prior know as Wi-Fi P2P). Test yours with [res/test-hardware-capabilities.sh](https://github.com/albfan/miraclecast/blob/master/res/test-hardware-capabilities.sh)

 - copy the dbus policy **res/org.freedesktop.miracle.conf** to `/etc/dbus-1/system.d/`

## Install

To compile MiracleCast, you can choose from [autotools](http://en.wikipedia.org/wiki/GNU_build_system) or [cmake](http://en.wikipedia.org/wiki/CMake):

Autotools:

    $ ./autogen.sh
    $ mkdir build
    $ cd build
    $ ../configure --prefix=/usr/local #avoid --prefix for a standard install

Cmake:

    $ mkdir build
    $ cd build
    $ cmake ..

Compile

    $ make

Test

    $ make check #only with autotools by now

Install 

    $ sudo make install

## Automatic interface selection with udev

If you want to select the interface to start miraclecast with, add a udev rule with the script [res/write-udev-rule.sh](https://github.com/albfan/miraclecast/blob/master/res/write-udev-rule.sh) and configure miraclecast with

    $ ../configure --enable-rely-udev

You can also choose the interface with  `--interface` option for miracle-wifid.

## Linux Flavours and general compilation instructions

### Ubuntu

This specific linux flavour is so hard to get miraclecast dependencies that an alternative repo was created to install systemd with dbus

https://github.com/albfan/systemd-ubuntu-with-dbus

At this time, ubuntu is on version 15.04 and systemd is stick on 219 version, use branch [systemd-219](https://github.com/albfan/miraclecast/tree/systemd-219) to compile miraclecast

> See specific instructions on that repo

### Arch linux

Use existing [AUR package](https://aur.archlinux.org/packages/miraclecast-github/). Remember to enable kdus to systemd-git dependency

    $ export _systemd_git_kdbus=--enable-kdbus

You can achieve installation using [yaourt](https://wiki.archlinux.org/index.php/Yaourt)

### Other flavours

If you feel confidence enough (since systemd is the entrypoint for an OS) extract instructions from arch linux AUR PKGBUILD:

- [systemd-kdbus](https://aur.archlinux.org/cgit/aur.git/tree/PKGBUILD?h=systemd-kdbus)
- [miraclecast](https://aur.archlinux.org/cgit/aur.git/tree/PKGBUILD?h=miraclecast)

## Documentation

Steps to use it as sink:

 1. shutdown wpa_supplicant

        $ sudo kill -9 $(ps -ef | grep wpa_supplican[t] | awk '{print $2}')
        # now you can use `res/kill-wpa.sh`

        >Remember to save your config to use with `res/normal-wifi.sh`
        >it will be easily located with `ps -ef | grep wpa_supplicant` on `-c` option.

 2. launch wifi daemon

        $ sudo miracle-wifid &

 3. launch sink control (your network card will be detected. here 3)

        $ sudo miracle-sinkctl
        [ADD]  Link: 3

 4. run WiFi Display on link: 

        > run 3

 5. Pair your machine with other miracast device (mirroring)

 6. See your screen device on this machine

Steps to use it as peer:

 1. Repeat steps 1 and 2 from "use as sink"

 2. launch wifi control

        $ sudo miracle-wifictl

 3. Enable visibility for other devices

 4. Locate them using scanning

        > psp-scan

 5. Apart from list, or show info with peer &lt;mac&gt; there's nothing useful here by now.

## Autocompletion

 Source [res/miraclecast-completion](https://github.com/albfan/miraclecast/blob/master/res/miraclecast-completion) for autocompletion

## License

This software is licensed under the terms of the GNU-LGPL license. Please see ./COPYING for further information.

## Credits

This software is originally maintained by: David Herrmann dh.herrmann@gmail.com 

This fork is maintained by: Alberto Fanjul albertofanjul@gmail.com

If you have any questions, do not hesitate to contact one of the maintainers.

## Additional urls

- Website: http://www.freedesktop.org/wiki/Software/miracle
- Original repo: git://people.freedesktop.org/~dvdhrm/miracle
- Fork repo: https://github.com/albfan/miraclecast

