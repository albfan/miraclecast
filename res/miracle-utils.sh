#http://lxr.linux.no/linux+v3.0/include/linux/if_arp.h#L67
ARPHRD_LOOPBACK=772

#
# Find all interfaces except loopback one
#
function find_choosable_networknames {
   for i in $( ls /sys/class/net )
   do 
      if [ $( cat /sys/class/net/$i/type ) != $ARPHRD_LOOPBACK ] 
      then
         echo $i
      fi
   done
}

#
# find wireless interfaces
#
function find_wireless_network_interfaces {
   for i in $( find_choosable_networknames )
   do 
      if [ -d /sys/class/net/$i/wireless ]
      then
         echo $i
      fi
   done
}

#
# show pci slot
#
function show_pci_slot {
   basename $(readlink /sys/class/net/$1/device) | cut -d: -f2 | sed 's/^0*//'
}

#
# test if interface is connected
#
function is_interface_connected {
   test x$( cat /sys/class/net/$1/carrier 2>/dev/null) = x1
}

#
# find wireless pci slot
#
function find_wireless_pci_slot {
   for i in $( find_wireless_network_interfaces )
   do
      show_pci_slot $i
   done
}

#
# find wireless pci slot
#
function find_wireless_ifindex {
   for i in $( find_wireless_network_interfaces )
   do
      show_ifindex $i
   done
}

#
# find wireless connected interfaces
#
function find_wireless_connected_network_interfaces {
   for i in $( find_wireless_network_interfaces )
   do 
      if is_interface_connected $i
      then
         echo $i
      fi
   done
}

#
# find physical for interface if exists
#
function find_physical_for_network_interface {
   PHY_INDEX=$(iw dev $1 info | grep wiphy | awk '{print $2}')
   if [ -n "$PHY_INDEX" ]
   then
      echo phy$PHY_INDEX
   fi
}

#
# find interface index for interface
#
function show_ifindex {
   IF_INDEX=$(iw dev $1 info | grep ifindex | awk '{print $2}')
   if [ -n "$IF_INDEX" ]
   then
      echo $IF_INDEX
   fi
}

#
# Check interface for P2P capabilities
#
function search_p2p_capabilities {
   WI_DEVICE=$1
   PHY_DEVICE=$(find_physical_for_network_interface $WI_DEVICE)

   if [ -z "$PHY_DEVICE" ]
   then
      echo "cannot find physical device for $WI_DEVICE"
      return
   fi

   if iw phy $PHY_DEVICE info | grep -Pzo "(?s)Supported interface modes.*Supported commands" | grep "P2P" &> /dev/null
   then
      echo $WI_DEVICE supports P2P
   else
      echo Sorry, $WI_DEVICE do not support P2P
      exit 1
   fi
}

#
# show wpa_supplicant command
#
function show_wpa_supplicant_process {
   ps -ef | grep "wpa_supplican[t] "
}

#
# show wpa_supplicant command
#
function show_wpa_supplicant_command {
   show_wpa_supplicant_process | awk '{print substr($0, index($0,$8))}'
}

#
# find wpa_supplicant pid
#
function find_wpa_supplicant_pid {
   show_wpa_supplicant_process | awk '{print $2}'
}

#
# checking if distro is archlinux
#
function check_archlinux_distro {
   test -f "/etc/arch-release"
}
#
# checking if distro is ubuntu
#
function check_ubuntu_distro {
   cat /proc/version | grep -i ubuntu
}
#
# checking if distro is debian
#
function check_debian_distro {
   cat /proc/version | grep -i debian
}

#
# ubuntu manager restarts automatically wpa_supplicant
#
function kill_ubuntu_network_manager {
   if check_ubuntu_distro || check_debian_distro
   then
      echo stopping NetworkManager
      sudo service NetworkManager stop
   fi
}

#
# arch linux manager restarts automatically wpa_supplicant
#
function kill_archlinux_network_manager {
   if check_ubuntu_distro
   then
      echo stopping NetworkManager
      sudo systemctl stop Network.service
   fi
}

#
# start ubuntu manager
#
function start_ubuntu_network_manager {
   if check_ubuntu_distro || check_debian_distro
   then
      echo starting NetworkManager
      sudo service NetworkManager start
   fi
}

#
# start arch linux manager
#
function start_archlinux_network_manager {
   if check_archlinux_distro
   then
      echo starting NetworkManager
      sudo systemctl start Network.service
   fi
}
