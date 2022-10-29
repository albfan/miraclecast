#!/bin/bash

DIRNAME=$(dirname $0)

. $DIRNAME/miracle-utils.sh

./$DIRNAME/kill-wpa.sh

start_network_manager
