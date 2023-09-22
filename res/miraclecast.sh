#!/bin/bash
#
# Run Miraclecast
#

# 1. capture argument
# 2. Abort if error on wifid
# 3. Runk sink with arg


miracle-wifid --log-level debug --log-date-time

miracle-sinkctl --log-level trace --log-date-time