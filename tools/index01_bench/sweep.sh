#!/bin/bash
# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0
# sweep.sh STAGE CFG... [-- bench args]: run_cfg.sh each staged config in turn; stops if the
# rig gets a CI job. Log: ~/pbl-power/sweep-STAGE.log; results: ~/pbl-power/scan-results/.
stage=$1; shift
cfgs=(); while [ $# -gt 0 ] && [ "$1" != -- ]; do cfgs+=("$1"); shift; done; shift
log=~/pbl-power/sweep-$stage.log
for c in "${cfgs[@]}"; do
  if pgrep -f Runner.Worker >/dev/null && pgrep -fl "Runner.Worker" | grep -qv vphone && pgrep -f "watch_server|run-release-e2e|pytest firmware" >/dev/null; then
    echo "CI job on the rig; stopping before $c" >> "$log"; exit 1
  fi
  echo "== $c $(date +%T)" >> "$log"
  ~/pbl-power/run_cfg.sh "$c" "$@" >> "$log" 2>&1 || { echo "FAILED $c" >> "$log"; failed=1; }
done
~/pbl/bin/pbl power off >/dev/null 2>&1
echo "SWEEP DONE $stage${failed:+ (with failures)}" >> "$log"
exit ${failed:-0}
