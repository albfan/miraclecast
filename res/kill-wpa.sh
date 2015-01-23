echo killing actual wpa_supplicant connection
sudo kill -9 $(ps -ef | grep wpa_supplican[t] | awk '{print $2}')
