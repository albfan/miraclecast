./kill-wpa.sh

CONFIG_FILE=${1:-/run/network/wpa_supplicant_wlp3s0.conf}
echo starting wpa_supplicant for normal connection
sudo wpa_supplicant -B -P /run/wpa_supplicant_wlp3s0.pid -i wlp3s0 -D nl80211,wext -c$CONFIG_FILE
