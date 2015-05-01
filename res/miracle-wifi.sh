#!/bin/bash

./kill-wpa.sh

. miracle-utils.sh

WIFI_NAMES=$(find_wireless_connected_network_interfaces)
WIFI_COUNT=$(echo "$WIFI_NAMES" | wc -l)

if [ 0 = $WIFI_COUNT ]
then
   echo There is no wireless devices connected
   # choose from any existing wireless interface
   WIFI_NAMES=$(find_wireless_network_interfaces)
   WIFI_COUNT=$(echo "$WIFI_NAMES" | wc -l)
fi

if [ 0 = $WIFI_COUNT ]
then
   echo There is no avaliable wireless devices
   exit 1
elif [ 1 = $WIFI_COUNT ]
then
   WIFI_NAME="$WIFI_NAMES"
elif [ 2 -le $WIFI_COUNT ]
then
   echo Choose wireless device:
   PS3="device: "
   QUIT="exit"
   select wifi_name in $WIFI_NAMES $QUIT
   do
      case $wifi_name
      in
      "$QUIT")
         exit
         ;;
      "")
         if [ "$REPLY" = $QUIT ]
         then
            exit
         else
            echo unknow $REPLY
         fi
         ;;
      *)
         WIFI_NAME=$wifi_name
         break
         ;;
      esac
   done
fi

echo starting wpa_supplicant for miraclecast on $WIFI_NAME
sudo wpa_supplicant -dd -B -u -i$WIFI_NAME -Dnl80211 -c$(dirname $0)/wpa.conf
