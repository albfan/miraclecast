#!/bin/bash

apt install cmake libglib2.0-dev libudev-dev libsystemd-dev libreadline-dev check libtool -y
apt install gstreamer1.0-plugins-bad gstreamer1.0-libav gstreamer1.0-tools -y

apt install vlc -y

rm -rf build/
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr .. 
make
make install
cd ..

echo "-- Installing: /root/.miraclecast"
cp res/.miraclecast /root/

echo "-- Installing: /usr/bin/run-vlc.sh"
cp res/run-vlc.sh /usr/bin/

echo "-- Installing: /etc/systemd/system/miracle-wifid.service"
cp systemd/system/miracle-wifid.service /etc/systemd/system/

echo "-- Installing: /etc/systemd/system/miracle-sink.service"
cp systemd/system/miracle-sink.service /etc/systemd/system/
