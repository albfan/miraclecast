# MiracleCast - Wifi-Display/Miracast Implementation

The MiracleCast project provides software to connect external monitors to your system via Wifi. It is compatible to the Wifi-Display specification also known as Miracast. MiracleCast implements the Display-Source as well as Display-Sink side.

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

 - **gstreamer**: MiracleCast relay on gstreamer to show cast its output. You can test if all needed is installed launching `res/test_viewer.sh`

 - copy the dbus policy **res/org.freedesktop.miracle.conf** to `/etc/dbus-1/system.d/`

## Install

  To compile MiracleCast, run the standard autotools commands:

```bash
    $ test -f ./configure || NOCONFIGURE=1 ./autogen.sh
    $ ./configure --prefix=/usr/local
    $ make
    $ sudo make install
    To compile and run the test applications, use:
    $ make check
```
### Ubuntu

This specific linux flavour is so hard to get miraclecast dependencies that an alternative repo was created to install systemd with dbus

https://github.com/albfan/systemd-ubuntu-with-dbus

> See specific instructions on that repo

### Arch linux

There is an AUR package to ease the install process. It's not complete at this time. See #6 to follow it's progress.

## Documentation

Steps to use it as sink:

 1. shutdown wpa_supplicant

        $ sudo kill -9 $(ps -ef | grep wpa_supplican[t] | awk '{print $2}')
        # now you can use `res/kill-wpa.sh`

 2. start wpa with config for miraclecast

        $ sudo wpa_supplicant -dd -B -iwlp3s0 -Dnl80211 -c wpa.conf
        # or `res/miracle-wifi.sh`

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

