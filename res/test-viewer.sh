#!/bin/bash
#
# test gstreamer plugins required for miraclecast
#

plugins=(udpsrc rtpjitterbuffer rtpmp2tdepay tsdemux h264parse avdec_h264 autovideosink)

echo testing plugins required:
echo

FAIL=

for plugin in ${plugins[@]}
do
   echo -n test $plugin...
   gst-inspect-1.0 $plugin &> /dev/null
   if [ $? != 0 ]
   then
      FAIL=1
      echo
      echo -e \\tgst plugin \"$plugin\" not available
   else
      echo -e " (passed)"
   fi
done

echo 

if [ -n "$FAIL" ]
then
   cat <<EOF 
Some plugins required for visualization are missed

Try installing packages "gst-plugins-bad, gst-plugins-base, gst-plugins-base-libs, gst-plugins-good, gst-plugins-ugly, gst-libav, gstreamer".

If that fails too, try:

$ vlc rtp://@:1991

EOF
else
   echo everything installed
fi
echo


