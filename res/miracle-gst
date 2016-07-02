#!/bin/bash

function help {
   local scriptname="$(basename $0)"
   cat >&2 <<EOF

$scriptname [options]

play rtp stream

Options:
   -r                   Resolution
   -s <Width>x<height>  Scale
   -d <level>           Log level for gst
   -p <port>            Port for stream
   -a                   Enables audio
   -h                   Show this help

Examples:

 # play stream on port 7236
 $ $scriptname -p 7236
 # play stream with resolution 800x600
 $ $scriptname -s 800x600
 # play stream with audio
 $ $scriptname -a
 # play stream with debug level 3
 $ $scriptname -d 3

EOF
}

DEBUG='0'
AUDIO='0'
SCALE='0'

while getopts "r:d:as:p:h" optname
  do
    case "$optname" in
      "h")
        help
        exit 0
        ;;
      "d")
        DEBUG=`echo ${OPTARG} | tr -d ' '`
        ;;
      "r")
        RESOLUTION=`echo ${OPTARG} | tr -d ' '`
        ;;
      "a")
        AUDIO='1'
        ;;
      "p")
        PORT=`echo ${OPTARG} | tr -d ' '`
        ;;
      "s")
        SCALE='1'
        WIDTH=`echo ${OPTARG} | tr -d ' ' | cut -dx -f 1`
        HEIGHT=`echo ${OPTARG} | tr -d ' ' | cut -dx -f 2`
        ;;
      "?")
        echo "Unknown option $OPTARG"
        ;;
      *)
        echo "Unknown parameter $OPTARG"
        help
        exit 1
        ;;
    esac
  done

RUN="/usr/bin/gst-launch-1.0 -v "
if [ $DEBUG != '0' ]
then
  RUN+="--gst-debug=${DEBUG} "
fi

RUN+="udpsrc port=$PORT caps=\"application/x-rtp, media=video\" ! rtpjitterbuffer latency=100 ! rtpmp2tdepay ! tsdemux "

if [ $AUDIO == '1' ]
then
  RUN+="name=demuxer demuxer. "
fi

RUN+="! queue max-size-buffers=0 max-size-time=0 ! h264parse ! avdec_h264 ! videoconvert ! "

if [ $SCALE == '1' ]
then
  RUN+="videoscale method=1 ! video/x-raw,width=${WIDTH},height=${HEIGHT} ! "
fi

RUN+="autovideosink "

if [ $AUDIO == '1' ]
then
  RUN+="demuxer. ! queue max-size-buffers=0 max-size-time=0 ! aacparse ! avdec_aac ! audioconvert ! audioresample ! autoaudiosink "
fi

echo "running: $RUN"
exec ${RUN}
