#!/bin/bash

. miracle-utils.sh

kill_ubuntu_network_manager

WPA_PID=$(find_wpa_supplicant_pid)
if [ -n "$WPA_PID" ]
then
   echo killing existing wpa_supplicant connection
   sudo kill -9 $WPA_PID
else
   echo cannot find wpa_supplicant connection to kill
   if [[ $_ == $0 ]]
   then
      exit 1
   fi
fi
