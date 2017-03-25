#!/bin/bash

function help {
   local scriptname="$(basename $0)"
   cat >&2 <<EOF

$scriptname [options]

play rtp stream with vlc

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

RUN="vlc rtp://@:$PORT"

echo "running: $RUN"
exec ${RUN}
