#!/bin/bash

function kill_child() {
  CHILDREN="$(ps -o pid= --ppid $$)"
  echo killing $CHILDREN
  kill $CHILDREN
}

IP=$1
shift
UIBC_PORT=$1
shift

echo $$

trap 'kill_child' SIGTERM

gstplayer $@ | miracle-uibcctl $IP $UIBC_PORT --daemon &
wait
