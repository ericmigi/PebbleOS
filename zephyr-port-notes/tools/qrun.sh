#!/usr/bin/env bash
# Launch a Pebble QEMU detached (survives the caller) with the SPP serial on
# 12344. Usage: qrun.sh <zephyr|ref> <elf>. Writes console to /tmp/q-console.log,
# monitor to /tmp/q-mon.sock. Kills any prior instance first.
set -u
KIND="${1:-zephyr}"; ELF="${2:?elf path}"
QEMU=~/dev/qemu-pebble-src/build/qemu-system-arm
pkill -9 -f 'qemu-system-arm.*pebble-emery' 2>/dev/null || true
sleep 1
rm -f /tmp/q-mon.sock /tmp/q-console.log
cp /home/taz/dev/PebbleOS/build/qemu_spi_flash.bin /tmp/q-spi.bin
AUDIO=""
[ "$KIND" = ref ] && AUDIO="-audiodev none,id=snd0"
MACH="pebble-emery"
[ "$KIND" = ref ] && MACH="pebble-emery,audiodev=snd0"
setsid "$QEMU" -rtc base=localtime -machine "$MACH" $AUDIO -display none \
  -kernel "$ELF" \
  -serial file:/tmp/q-console.log -serial tcp::12344,server=on,wait=off -serial null \
  -monitor unix:/tmp/q-mon.sock,server=on,wait=off \
  -drive if=mtd,format=raw,file=/tmp/q-spi.bin >/tmp/q-qemu.log 2>&1 < /dev/null &
disown
for i in $(seq 20); do [ -S /tmp/q-mon.sock ] && break; sleep 1; done
sleep 12
echo "listen=$(ss -tlnp 2>/dev/null | grep -c 12344) up=$(grep -c SYS_APP_LOOP /tmp/q-console.log 2>/dev/null)"
