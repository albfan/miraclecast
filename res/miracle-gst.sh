#!/bin/bash

DEBUG='0'
AUDIO='0'
SCALE='0'

while getopts "d:as" optname
  do
    case "$optname" in
      "d")
        DEBUG=`echo ${OPTARG} | tr -d ' '`
        ;;
      "a")
        AUDIO='1'
        ;;
      "s")
        SCALE='1'
        ;;
      "?")
        echo "Unknown option $OPTARG"
        ;;
      *)
      # Should not occur
        echo "Unknown error while processing options"
        ;;
    esac
  done

RUN="/usr/bin/gst-launch-1.0 -v "
if [ $DEBUG != '0' ]
then
  RUN+="--gst-debug=${DEBUG} "
fi

RUN+="udpsrc port=1991 caps=\"application/x-rtp, media=video\" ! rtpjitterbuffer latency=100 ! rtpmp2tdepay ! tsdemux "

if [ $AUDIO == '1' ]
then
  RUN+="name=demuxer demuxer. "
fi

RUN+="! queue max-size-buffers=0 max-size-time=0 ! h264parse ! avdec_h264 ! videoconvert ! "

if [ $SCALE == '1' ]
then
  RUN+="videoscale method=1 ! video/x-raw,width=1280,height=800 ! "
fi

RUN+="autovideosink "

if [ $AUDIO == '1' ]
then
  RUN+="demuxer. ! queue max-size-buffers=0 max-size-time=0 ! aacparse ! avdec_aac ! audioconvert ! audioresample ! autoaudiosink "
fi

echo "running: $RUN"
exec ${RUN}
