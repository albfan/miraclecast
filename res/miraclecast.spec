%global commit 264a222d242734da369ca287aa6cfc6ca4f1f7bf
%global shortcommit %(c=%{commit}; echo ${c:0:7})

Name:           miraclecast
Version:        1.0
Release:        2.git%{shortcommit}%{?dist}
Summary:        Connect external monitors to your system via Wi-Fi Display (miracast)
License:        LGPLv2
URL:            https://github.com/albfan/miraclecast
Source0:        https://github.com/albfan/miraclecast/archive/%{commit}/%{name}-%{shortcommit}.tar.gz

BuildRequires:  autoconf
BuildRequires:  automake
BuildRequires:  libtool

BuildRequires:  glib2-devel
BuildRequires:  readline-devel
BuildRequires:  systemd-devel

# "Recommends" is stronger than "Suggests", and gets installed by default by DNF.

# for gstplayer
Recommends: python3-gobject-base

# for miracle-gst (/usr/bin/gst-launch-1.0)
Suggests: gstreamer

# for miracle-omxplayer
Suggests: omxplayer

%description
The MiracleCast project provides software to connect external monitors to your
system via Wi-Fi. It is compatible to the Wi-Fi Display specification also
known as Miracast. MiracleCast implements the Display-Source as well as
Display-Sink side.

The Display-Source side allows you to connect external displays to your system
and stream local content to the device. A lot of effort is put into making
this as easy as connecting external displays via HDMI.

On the other hand, the Display-Sink side allows you to create Wi-Fi capable
external displays yourself. You can use it on your embedded devices or even on
full desktops to allow other systems to use your device as external display.

%prep
%autosetup -n %{name}-%{commit}

%build
autoreconf -fiv
%configure
%make_build

%install
%make_install

%files
%license COPYING LICENSE_gdhcp LICENSE_htable LICENSE_lgpl
%doc README.md
%config(noreplace) %{_sysconfdir}/dbus-1/system.d/org.freedesktop.miracle.conf
%{_bindir}/*
%{_datadir}/bash-completion/completions/*

%changelog
* Wed Jan 05 2022 Korenberg Mark
- Fix .spec-file, bump version

* Wed Nov 21 2018 Graham White
- first build
