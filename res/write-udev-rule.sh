#!/bin/bash

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
   echo choose device to use with miraclecast:
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

read -p "Provide order to udev rule (default 10): " -e -i 10 ORDER

RULE_FILE="/etc/udev/rules.d/${ORDER}-network.rules"

echo "...Press enter to finish (sorry, don't know why. It is related with sudo tee)"

cat | sudo tee "${RULE_FILE}" &>/dev/null <<-EOF
	SUBSYSTEM=="net", ACTION=="add", ATTR{address}=="$(cat /sys/class/net/${ETHERNAME}/address)", NAME="${ETHERNAME}", TAGS+="miracle"
EOF

echo file "${RULE_FILE}" writed

cat "${RULE_FILE}"
