# MiracleCast - Wifi-Display/Miracast Implementation

[![Join the chat at https://gitter.im/albfan/miraclecast](https://badges.gitter.im/albfan/miraclecast.svg)](https://gitter.im/albfan/miraclecast?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)
[![Build Status](https://semaphoreci.com/api/v1/albfan/miraclecast-2/branches/master/badge.svg)](https://semaphoreci.com/albfan/miraclecast-2)
[![Coverage Status](https://coveralls.io/repos/github/albfan/miraclecast/badge.svg?branch=master)](https://coveralls.io/github/albfan/miraclecast?branch=master)

The MiracleCast project provides software to connect external monitors to your system via Wi-Fi. It is compatible with the Wifi-Display specification also known as Miracast. MiracleCast implements the Display-Source as well as Display-Sink side.

The Display-Source side allows you to connect external displays to your system and stream local content to the device. A lot of effort is put into making this as easy as connecting external displays via HDMI.

On the other hand, the Display-Sink side allows you to create wifi-capable external displays yourself. You can use it with your embedded devices or even on full desktops to allow other systems to use your device as an external display.


## Requirements

The MiracleCast projects requires the following software to be installed:
 - **systemd**: A system management daemon. It is used for device-management (udev), dbus management (sd-bus) and service management.
    Systemd >= 221 will work out of the box. For earlier versions, systemd must be compiled with --enable-kdbus, even though kdbus isn't used...the independent, experimental sd-libraries are used.
    *required*: >=systemd-213

 - **glib**: A utility library. Used by the current DHCP implementation. This will be removed once sd-dns gains DHCP-server capabilities.
    *required*: ~=glib2-2.38 (might work with older releases, untested..)

 - **readline**: A library which is used to provide a command line interface to control wifid, sink, etc..

 - **check**: Test-suite for C programs. Used for optional tests of the MiracleCast code base.
    *optional*: ~=check-0.9.11 (might work with older releases, untested..)

 - **gstreamer**: MiracleCast relies on gstreamer to cast its output. You can test for all requirements by launching [res/test-viewer.sh](https://github.com/albfan/miraclecast/blob/master/res/test-viewer.sh).

   gstreamer plugins: plugins you need in order to run sinkctl or dispctl
     - gstreamer-plugins-base
     - gstreamer-plugins-good
     - gstreamer-plugins-bad
     - gstreamer-plugins-ugly
     - gstreamer-plugins-vaapi
     - gstreamer-plugins-libav

 - **P2P Wi-Fi device** Although widespread these days, there are some devices not compatible with [Wi-Fi Direct](http://en.wikipedia.org/wiki/Wi-Fi_Direct) (previously know as Wi-Fi P2P). Test yours with [res/test-hardware-capabilities.sh](https://github.com/albfan/miraclecast/blob/master/res/test-hardware-capabilities.sh)

 - **DBus Policy** The dbus policy, [res/org.freedesktop.miracle.conf](https://github.com/albfan/miraclecast/blob/master/res/org.freedesktop.miracle.conf), should be copied to "/etc/dbus-1/system.d/". The installation process should do this automatically.

## Build and install

To compile MiracleCast, you can choose from:

 - [autotools](http://en.wikipedia.org/wiki/GNU_build_system)
 - [cmake](http://en.wikipedia.org/wiki/CMake)
 - [meson](http://mesonbuild.com/)

See more info on wiki [Building](https://github.com/albfan/miraclecast/wiki/Building)

## Interface selection with udev

If you want to set the default interface used when miraclecast starts, add a udev rule with the script [res/write-udev-rule.sh](https://github.com/albfan/miraclecast/blob/master/res/write-udev-rule.sh) and configure miraclecast with:

    $ ../configure --enable-rely-udev

You can also choose the interface by using the '--interface' option for miracle-wifid.

## Linux Flavours and general compilation instructions

### Ubuntu

Check your systemd version with:

    $ systemctl --version

If you are on 221 or above your systemd has kdbus enabled.

If you are below 221, an alternative repo was created to install systemd with dbus

https://github.com/albfan/systemd-ubuntu-with-dbus

There are interface changes starting with systemd 219.  If you are below that version, use branch [systemd-219](https://github.com/albfan/miraclecast/tree/systemd-219) to compile miraclecast

> See specific instructions on that repo

### Arch linux

Use existing [AUR package](https://aur.archlinux.org/packages/miraclecast/). Remember to enable kdus to systemd-git dependency if you are below 221 systemd.

    $ export _systemd_git_kdbus=--enable-kdbus

You can achieve installation using [yaourt](https://wiki.archlinux.org/index.php/Yaourt)

### Other flavours

If you feel confidence enough (since systemd is the entrypoint for an OS) extract instructions from arch linux AUR PKGBUILD:

- [systemd-kdbus](https://aur.archlinux.org/cgit/aur.git/tree/PKGBUILD?h=systemd-kdbus)
- [miraclecast](https://aur.archlinux.org/cgit/aur.git/tree/PKGBUILD?h=miraclecast)

## Documentation

Steps to activate MiracleCast services:

 1. Check the status of the MiracleCast services

        $ systemctl status miracle-dispd.service
        # This will display the current status of the MiracleCast Display Daemon
        # It works with the MiracleCast WiFi Daemon

        $ systemctl status miracle-dispd.service
        # This will display the current status of the MiracleCast WiFi Daemon

 2. If needed, start the MiracleCast services

        $ sudo systemctl start miracle-dispd.service
        # This will start the the MiracleCast Display Daemon
        # It will also start the MiracleCast WiFi Daemon

        # You may control them independently as needed

Steps to use MiracleCast as a sink:

 1. The MiracleCast WiFi Daemon must be started and active.  See above.

 2. Launch the MiracleCast Sink control (your network card will be detected as a link; the example link here is 3)

        $ sudo miracle-sinkctl
        [ADD]  Link: 3
        [miraclectl] #

 3. Set the link to managed (this will force the network card to be taken by MiracleCast from NetworkManager after a few seconds)

        [miraclectl] # set-managed 3 yes

 4. Run WiFi Display on the link:

        [miraclectl] # run 3

 5. Pair your machine with other Miracast device (mirroring)

 6. See your Miracast screen device on this machine

 7. When finished, set the link to unmanaged--this will release the network card to NetworkManager

        [miraclectl] # set-managed 3 no

 8. Exit the MiracleCast Sink control

        [miraclectl] # quit

Steps to use MiracleCast as a display source:

 1. The MiracleCast Display Daemon must be started and active.  See above.

 2. Launch the MiracleCast Display control with connection options "-i" for your network card as identified by "ifconfig" and "-p" for your peers MAC address (this will force the network card to be taken by MiracleCast from NetworkManager)

        $ sudo miracle-dispctl -i myNICid -p 00:00:00:00:00:00

 3. The MiracleCast Display control will use the daemons to automate a display connection to the peer

        miracle-dispctl: peer-mac=00:00:00:00:00:00
        miracle-dispctl: display=:0
        miracle-dispctl: authority=/home/someuser/.Xauthority
        miracle-dispctl: interface=myNICid
        miracle-dispctl: wfd_subelemens=000600001c4400c8
        miracle-dispctl: monitor-num=0
        miracle-dispctl: wait for peer '00:00:00:00:00:00'...

 4. Exit the MiracleCast Display control

        ctrl-C

        ^Cmiracle-dispctl: failed to cast to wireless display: Operation was cancelled
        miracle-dispctl: Bye

Steps to use MiracleCast as a peer:

 1. The MiracleCast WiFi Daemon must be started and active.  See above.

 2. Launch the MiracleCast WiFi control (your network card will be detected as a link; the example link here is 3)

        $ sudo miracle-wifictl
        [ADD]  Link: 3
        [miraclectl] #

 3. Select the link to use--simplifies the following commands as the selected link is assumed

        [miraclectl] # select 3

 4. Set a friendly name for other peers to see

        [miraclectl] # set-friendly-name SomeAwesomeName

 4. Set the link to managed--this will force the network card to be taken by MiracleCast from NetworkManager after a few seconds

        [miraclectl] # set-managed yes

 5. Commands:

   Locate other peers using P2P scanning

        [miraclectl] # p2p-scan

   Stop P2P scanning

        [miraclectl] # p2p-scan stop

   List other peers

        [miraclectl] # list

   Show detailed information on other peers

        [miraclectl] # show 4

   Connect to a peer--the sink may require a provision or pin (not shown, see help)

        [miraclectl] # connect 4

   Disconnect from a peer

        [miraclectl] # disconnect 4

 6. While connected, you may use the connection as a source with software using the connection such as miracle-gst and miracle-omxplayer

 7. Exit the MiracleCast WiFi control

        [miraclectl] # quit

## UIBC

> The User Input Back Channel (UIBC) is an optional WFD feature that when implemented facilitates communication of user inputs to a User Interface present at the WFD Sink back to the WFD Source.

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
