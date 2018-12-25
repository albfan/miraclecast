%global commit c3c868e523f450ad4f0d77f5484a3b61f08120b7
%global shortcommit %(c=%{commit}; echo ${c:0:7})

Name:           miraclecast
Version:        1.0
Release:        1.git%{shortcommit}%{?dist}
Summary:        Connect external monitors to your system via Wifi-Display
License:        LGPL
URL:            https://github.com/albfan/miraclecast
Source0:        https://github.com/albfan/miraclecast/archive/%{commit}/%{name}-%{shortcommit}.tar.gz
BuildRequires:  autoconf, automake, libtool
BuildRequires:  readline-devel, glib2-devel, systemd-devel

%description
The MiracleCast project provides software to connect external monitors to your system via Wi-Fi. It is compatible to the Wifi-Display specification also known as Miracast. MiracleCast implements the Display-Source as well as Display-Sink side.

The Display-Source side allows you to connect external displays to your system and stream local content to the device. A lot of effort is put into making this as easy as connecting external displays via HDMI.

On the other hand, the Display-Sink side allows you to create wifi-capable external displays yourself. You can use it on your embedded devices or even on full desktops to allow other systems to use your device as external display.


%prep
%autosetup -n %{name}-%{shortcommit}


%build
mkdir build
cd build
../autogen.sh g --prefix=%{_prefix}
%make_build


%install
rm -rf $RPM_BUILD_ROOT
cd build
%make_install


%files
%license COPYING LICENSE_gdhcp LICENSE_htable LICENSE_lgpl
%doc README.md
%{_sysconfdir}/dbus-1/system.d/org.freedesktop.miracle.conf
%{_bindir}/miracle-wifictl
%{_bindir}/miracle-omxplayer
%{_bindir}/miracle-gst
%{_bindir}/miracle-sinkctl
%{_bindir}/miracled
%{_bindir}/miracle-dhcp
%{_bindir}/miracle-uibcctl
%{_bindir}/miracle-wifid
%{_bindir}/gstplayer
%{_bindir}/uibc-viewer
%{_datadir}/bash-completion/completions/miracle-wifictl
%{_datadir}/bash-completion/completions/miracle-sinkctl
%{_datadir}/bash-completion/completions/miracle-wifid


%changelog
* Wed Nov 21 2018 Graham White
- first build
