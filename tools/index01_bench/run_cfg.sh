#!/bin/bash
# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0
# run_cfg.sh NAME [bench args...]: flash ~/obelix-flash/scan/NAME, then run index_bench.py on it.
set -u
name=$1; shift
d=$HOME/obelix-flash/scan/$name
export PATH=/opt/homebrew/bin:$PATH
STEP=$HOME/fwtest-pbltest/qa-release/bin/step.py   # pbl power version
flash() {
  for t in 1 2 3; do
    (cd ~/obelix-flash && /usr/bin/python3 "$STEP" write_flash "$1" 2>&1 | tail -1) | grep -q "STEP_EXIT 0" && return 0
    sleep 3
  done
  return 1
}
flash "$d/system_resources.pbpack@0x12620000" && flash "$d/pebbleos.hex" || { echo "FLASH FAILED $name"; exit 1; }
echo "flashed $name"
cd ~/pbl-power && venv/bin/python index_bench.py --name "$name" --out ~/pbl-power/scan-results "$@"
