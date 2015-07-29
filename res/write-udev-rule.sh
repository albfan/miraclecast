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

NUMBER=10

cat > /etc/udev/rules.d/${NUMBER}-network.rules << EOF
   SUBSYSTEM=="net", ACTION=="add", ATTR{address}=="$(cat /sys/class/net/${ETHERNAME}/address)", NAME="${ETHERNAME}", TAGS+="miracle"
EOF
