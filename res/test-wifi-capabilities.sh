#/bin/bash

echo You must see P2P-Go and P2P-Client below this line for a valid miraclecast device:

iw list | grep -P "(?s)Supported interface.*Supported commands" | grep "P2P"
