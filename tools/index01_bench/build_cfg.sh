#!/bin/bash
# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0
# build_cfg.sh NAME ITVL_MS WIN_MS FILTER_DUPS PASSIVE: release obelix@dvt build of the Index 01
# scan with those settings, staged on the rig at ~/obelix-flash/scan/NAME/.
set -eu
name=$1 itvl=$2 win=$3 dups=$4 passive=$5
[[ $name =~ ^[A-Za-z0-9._-]+$ ]] || { echo "bad name '$name'" >&2; exit 2; }
for n in "$itvl" "$win" "$dups" "$passive"; do [[ $n =~ ^[0-9]+$ ]] || { echo "bad number '$n'" >&2; exit 2; }; done
cd "$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
f=subsys/bluetooth/shell.c
sed -i '' -E "s/^#define INDEX01_SCAN_ITVL_MS .*/#define INDEX01_SCAN_ITVL_MS $itvl/; \
  s/^#define INDEX01_SCAN_WIN_MS .*/#define INDEX01_SCAN_WIN_MS $win/; \
  s/^#define INDEX01_SCAN_FILTER_DUPS .*/#define INDEX01_SCAN_FILTER_DUPS $dups/; \
  s/^#define INDEX01_SCAN_PASSIVE .*/#define INDEX01_SCAN_PASSIVE $passive/" $f
grep -E "^#define INDEX01_SCAN_" $f
"${PBL:-pbl}" build -b build-obelix-dvt-rel-index > "/tmp/build-$name.log" 2>&1 || { tail -20 "/tmp/build-$name.log"; exit 1; }
ssh -o BatchMode=yes unicorn-mac-3@100.67.190.1 "mkdir -p ~/obelix-flash/scan/$name"
scp -q build-obelix-dvt-rel-index/pebbleos.hex build-obelix-dvt-rel-index/system_resources.pbpack \
  unicorn-mac-3@100.67.190.1:obelix-flash/scan/$name/
echo "staged $name"
