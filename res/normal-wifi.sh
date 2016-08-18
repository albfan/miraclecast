#!/bin/bash

./kill-wpa.sh

. miracle-utils.sh

ETHER_NAMES=$(find_choosable_networknames)

ETHER_COUNT=$(echo "$ETHER_NAMES" | wc -l)

if [ 0 = $ETHER_COUNT ]
then
   echo There is no net devices avaliable
   exit 1
elif [ 1 = $ETHER_COUNT ]
then
   ETHERNAME="$ETHER_NAMES"
elif [ 2 -le $ETHER_COUNT ]
then
   echo choose device for normal connection:
   QUIT="exit"
   select et_name in $ETHER_NAMES $QUIT
   do
      case $et_name
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
          ETHERNAME=$et_name
          break
          ;;
        esac
   done
fi

# default path for config file
CONFIG_FILE=${1:-/run/network/wpa_supplicant_${ETHERNAME}.conf}


echo starting wpa_supplicant for normal connection
if check_ubuntu_distro || check_debian_distro
then
    start_ubuntu_network_manager
    sudo wpa_supplicant -B -u -s -O /var/run/wpa_supplicant
else
    sudo wpa_supplicant -B -u -P /run/wpa_supplicant_${ETHERNAME}pid -i ${ETHERNAME} -D nl80211 -c$CONFIG_FILE
fi

