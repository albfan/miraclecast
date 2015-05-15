#!/bin/bash

. miracle-utils.sh

WIFI_NAMES=$(find_wireless_network_interfaces)
WIFI_COUNT=$(echo "$WIFI_NAMES" | wc -l)

if [ 0 = $WIFI_COUNT ]
then
   echo There is no wireless devices avaliable
   exit 1
elif [ 1 = $WIFI_COUNT ]
then
   WIFI_NAME="$WIFI_NAMES"
elif [ 2 -ge $WIFI_COUNT ]
then
   echo Choose wireless device:
   PS3="device: "
   QUIT="exit"
   select wi_name in $WIFI_NAMES $QUIT
   do
      case $wi_name
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
         WIFI_NAME=$wi_name
         break
         ;;
      esac
   done
fi

search_p2p_capabilities $WIFI_NAME


