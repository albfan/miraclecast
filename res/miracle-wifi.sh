./kill-wpa.sh

echo starting wpa_supplicant for miraclecast
sudo wpa_supplicant -dd -B -iwlp3s0 -Dnl80211 -c wpa.conf
