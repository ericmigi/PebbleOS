#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
nimble="$root/third_party/nimble"
output_dir="$root/build-p3/sprf-bond-selfcheck"

mkdir -p "$output_dir"
cc -std=c11 -Wall -Wextra -Werror -fshort-enums \
  -DSPRF_BOND_HOST_TEST \
  -I"$root/zephyr-port-apps/fw/include" \
  -I"$root/include" \
  -I"$nimble/mynewt-nimble/porting/npl/dummy/include" \
  -I"$nimble/port/include" \
  -I"$nimble/port/include/sf32lb52" \
  -I"$nimble/mynewt-nimble/nimble/include" \
  -I"$nimble/mynewt-nimble/nimble/host/include" \
  -I"$nimble/mynewt-nimble/porting/nimble/include" \
  "$root/zephyr-port-apps/fw/src/sprf_bond.c" \
  "$root/zephyr-port-apps/fw/tests/sprf_bond_selfcheck.c" \
  -o "$output_dir/sprf_bond_selfcheck"

"$output_dir/sprf_bond_selfcheck"
