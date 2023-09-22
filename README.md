# MiracleCast - Wifi-Display/Miracast Implementation

[![Join the chat at https://gitter.im/albfan/miraclecast](https://badges.gitter.im/albfan/miraclecast.svg)](https://gitter.im/albfan/miraclecast?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)
[![Semaphore CI Build Status](https://albfan.semaphoreci.com/badges/miraclecast/branches/master.svg?style=shields)](https://albfan.semaphoreci.com/projects/miraclecast)
[![Travis CI Build Status](https://travis-ci.org/albfan/miraclecast.svg?branch=master)](https://travis-ci.org/albfan/miraclecast)
[![Coverage Status](https://coveralls.io/repos/github/albfan/miraclecast/badge.svg?branch=master)](https://coveralls.io/github/albfan/miraclecast?branch=master)

The MiracleCast project provides software to connect external monitors to your system via Wi-Fi. It is compatible to the Wifi-Display specification also known as Miracast. MiracleCast implements the Display-Source as well as Display-Sink side.

The Display-Source side allows you to connect external displays to your system and stream local content to the device. A lot of effort is put into making this as easy as connecting external displays via HDMI. *Note: This is not implemented yet. Please see [#4](../../issues/4).*

On the other hand, the Display-Sink side allows you to create wifi-capable external displays yourself. You can use it on your embedded devices or even on full desktops to allow other systems to use your device as external display.

## Steps to Setup MiracleCast on Ving NUC

In the root of the project directory, run `sudo ./install.sh`

Steps to run:

1. Make sure 'NetworkManger', 'wpa_supplicant' and 'AP' are all stopped/killed.
2. Make sure no instance of 'miracle-wifid' or 'miracle-sinkctl' are already running.
3. Run `sudo miracle-wifid --log-level trace --log-date-time` in a terminal.
4. Run `sudo miracle-sinkctl -e run-vlc.sh --log-level trace --log-journal-level trace --log-date-time -- set-friendly-name VingMiracle` in a separate terminal.


Now you should be able to cast your Windows 10/11 devices on Ving NUC.

## Requirements

The MiracleCast projects requires the following software to be installed:
 - **systemd**: A system management daemon. It is used for device-management (udev), dbus management (sd-bus) and service management.
    Systemd >= 221 will work out of the box. For earlier versions systemd must be compiled with --enable-kdbus, even though kdbus isn't used, but only the independent, experimental sd-libraries.
    *required*: >=systemd-213

 - **glib**: A utility library. Used by the current DHCP implementation. Will be removed once sd-dns gains DHCP-server capabilities.
    *required*: ~=glib2-2.38 (might work with older releases, untested..)

 - **gstreamer**: MiracleCast rely on gstreamer to show cast its output. You can test if all needed is installed launching [res/test-viewer.sh](https://github.com/albfan/miraclecast/blob/master/res/test-viewer.sh)

 - **wpa_supplicant**: MiracleCast spawns wpa_supplicant with a custom config.

 - **P2P Wi-Fi device** Although widespread these days, there are some devices not compatible with [Wi-Fi Direct](http://en.wikipedia.org/wiki/Wi-Fi_Direct) (prior know as Wi-Fi P2P). Test yours with [res/test-hardware-capabilities.sh](https://github.com/albfan/miraclecast/blob/master/res/test-hardware-capabilities.sh)

 - **check**: Test-suite for C programs. Used for optional tests of the MiracleCast code base.
    *optional*: ~=check-0.9.11 (might work with older releases, untested..)

 - copy the dbus policy **res/org.freedesktop.miracle.conf** to `/etc/dbus-1/system.d/`

## Build and install

To compile MiracleCast, you can choose from:

 - [autotools](http://en.wikipedia.org/wiki/GNU_build_system)
 - [cmake](http://en.wikipedia.org/wiki/CMake)
 - [meson](http://mesonbuild.com/)

See more info on wiki [Building](https://github.com/albfan/miraclecast/wiki/Building)

## Automatic interface selection with udev

If you want to select the interface to start miraclecast with, add a udev rule with the script [res/write-udev-rule.sh](https://github.com/albfan/miraclecast/blob/master/res/write-udev-rule.sh) and configure miraclecast with

    $ ../configure --enable-rely-udev

You can also choose the interface with  `--interface` option for miracle-wifid.

## Linux Flavours and general compilation instructions

### Ubuntu

Check your systemd version with:

    $ systemctl --version
    
If you are on 221 or above your systemd has kdbus enabled.
 
If you are below 221, an alternative repo was created to install systemd with dbus

https://github.com/albfan/systemd-ubuntu-with-dbus

See there was interface changes on systemd 219, if you are below that version, use branch [systemd-219](https://github.com/albfan/miraclecast/tree/systemd-219) to compile miraclecast

> See specific instructions on that repo

### Arch linux

Use existing [AUR package](https://aur.archlinux.org/packages/miraclecast-git/). Remember to enable kdbus to systemd-git dependency if you are below 221 systemd.

    $ export _systemd_git_kdbus=--enable-kdbus

You can achieve installation using [yaourt](https://wiki.archlinux.org/index.php/Yaourt)

### Other flavours

If you feel confidence enough (since systemd is the entrypoint for an OS) extract instructions from arch linux AUR PKGBUILD:

- [systemd-kdbus](https://aur.archlinux.org/cgit/aur.git/tree/PKGBUILD?h=systemd-kdbus)
- [miraclecast](https://aur.archlinux.org/cgit/aur.git/tree/PKGBUILD?h=miraclecast)

## Documentation

### Steps to use it as sink

 1. shutdown wpa_supplicant and NetworkManager

        $ systemctl stop NetworkManager.service
        $ systemctl stop wpa_supplicant.service 

 2. launch wifi daemon

        $ sudo miracle-wifid &

 3. launch sink control (your network card will be detected. here 3)

        $ sudo miracle-sinkctl
        [ADD]  Link: 3

 4. run WiFi Display on link: 

        > run 3

 5. Pair your machine with other miracast device (mirroring)

 6. See your screen device on this machine

### Steps to use it as peer

 1. Repeat steps 1 and 2 from "use as sink"

 2. launch wifi control

        $ sudo miracle-wifictl

 3. Enable visibility for other devices

 4. Locate them using scanning

        > p2p-scan

 5. Apart from list, or show info with peer &lt;mac&gt; there's nothing useful here by now. For a Q&D see [Using as peer](https://github.com/albfan/miraclecast/issues/4)

## UIBC

> The User Input Back Channel (UIBC) is an optional WFD feature that when implemented facilitates communication of user inputs to a User Interface, present at the WFD Sink, to the WFD Source.

To use it just add `--uibc` on `miracle-sinkctl` startup. Single mouse events and key events are implemented.

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
- Technical spec: https://www.wi-fi.org/file/wi-fi-display-technical-specification-v11 (free registration required)
