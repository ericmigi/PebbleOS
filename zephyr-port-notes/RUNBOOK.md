# Zephyr-on-obelix RUNBOOK — reproduce from scratch

Everything needed to rebuild, flash, and photograph the Sliding Text watchface
running on real obelix hardware under Zephyr. State captured: the North Star is
green (real PBW renders real Gotham, upright, PT time, on the physical panel).

## 0. Repos / branches

- **This worktree**: `~/dev/pebbleos-zephyr`, branch `zephyr/obelix` (pushed to
  `fork` = ericmigi/PebbleOS). Holds the port code: `lib/os/*_zephyr.c`, the
  `pbl/os` seam, the FreeRTOS-seam routing, and the standalone demo apps under
  `zephyr-port-apps/` (kernel, gfx, loader, watchface, watchface_sandboxed).
- **Zephyr fork workspace**: `~/dev/pblboot-ws` (a west workspace).
  - `~/dev/pblboot-ws/zephyr` = coredevices/zephyr fork. Branch `pt2-display`
    carries: the `pt2` board (obelix), the **sandbox-spike arch hooks**
    (`CONFIG_ARM_CUSTOM_SVC_HOOK` + `..._THREAD_RESTORE_HOOK`, commits
    `81885814` + `a636165b`), the **JDI display driver**
    (`drivers/display/sf32lb_jdi.c` + pt2 dts panel node), the SF32 PM contract,
    and the nPM1300 sample. (These fork commits are NOT pushed to ericmigi; they
    live on the local fork checkout. TODO: push the fork or fold into pblboot.)
- Rig flash recipe (authoritative, known-good 100%): coredevices/unicorn
  `OBELIX-FLASH-GUIDE.md` and the workflow
  `.github/workflows/one-off-pebbleos-test.yml`. If flashing ever misbehaves,
  go back to that doc.

## 1. Toolchain / build environment

Worktree is a self-contained build env:
- `~/dev/pebbleos-zephyr/.venv` (real venv, `pip install -r requirements.txt`)
  + all submodules initialized. Used for the **FreeRTOS** obelix build.
- `~/dev/pblboot-ws/.venv` + gnuarmemb ARM toolchain for the **Zephyr** builds.

Zephyr build env (every west build):
```sh
export ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb
export GNUARMEMB_TOOLCHAIN_PATH=$(dirname $(dirname $(which arm-none-eabi-gcc)))
```

Build a demo app (example: the watchface):
```sh
cd ~/dev/pblboot-ws
.venv/bin/west build -b pt2 ~/dev/pebbleos-zephyr/zephyr-port-apps/watchface \
  -d ~/dev/pebbleos-zephyr/build-watchface --pristine
```
Apps + their build dirs:
- kernel spine  -> zephyr-port-apps/kernel        -> build-kernel   (TICK path)
- graphics      -> zephyr-port-apps/gfx           -> build-gfx      (GFX_CRC)
- PBW loader    -> zephyr-port-apps/loader        -> build-loader   (LOADER_OK)
- watchface     -> zephyr-port-apps/watchface     -> build-watchface (the demo)
- sandboxed     -> zephyr-port-apps/watchface_sandboxed -> build-wfsb (unpriv, needs hw verify)

FreeRTOS regression build (must stay green — never break the shipping firmware):
```sh
cd ~/dev/pebbleos-zephyr
env PATH="$PWD/.venv/bin:$PATH" ./pbl configure --board obelix@pvt
env PATH="$PWD/.venv/bin:$PATH" ./pbl build      # -> build/pebbleos.hex
```
Gotcha: never symlink submodules from the main repo then `rm` them — it deletes
the superproject glue files (`third_party/{tinymt,qr_code_generator,speex}/*`);
restore with `git checkout -- .`.

## 2. The rig (unicorn-mac-1)

- Host: MacBook Air, Tailscale `100.87.44.126`, user `unicorn-mac-1`, pass
  `pebblepass` (password auth only; pubkey OFF). Passwordless `sudo`.
- SSH (retry on transient "Permission denied" — sshd rate-limits):
  ```sh
  sshpass -p pebblepass ssh -o PubkeyAuthentication=no \
    -o PreferredAuthentications=password unicorn-mac-1@100.87.44.126 '<cmd>'
  ```
  scp needs `-O`.
- Watch: obelix PVT. UART `/dev/cu.usbmodem5B7A1354731` @ **1000000 baud**.
  Keep `rts=False` (RTS resets the SoC).
- **PPK2 is the only power source**, owned by the `ppk2d.py` daemon (HTTP
  :8843: /status /on /off /cycle?off_ms= /measure?seconds=). NEVER open the PPK2
  serial directly — a second holder corrupts the stream.
- Flash workhorse: `~/obelix-flash/step.py` (power-cycles via PPK2, catches the
  SF32 ROM boot window, runs sftool). One power-cycle per invocation.
  ```sh
  # dev loop (fw already boots): resources only if changed, else fw alone
  cd obelix-flash && python3 step.py write_flash shellz/<image>.hex
  ```
- Boot/UART capture: `~/obelix-flash/boot_capture.py <baud> <secs>` (cycles via
  ppk2d, reads UART). `shell_probe.py` drives the Zephyr shell (10ms/char pacing).

## 3. Flash + verify a build (the loop)

```sh
scp -O build-watchface/zephyr/zephyr.hex ...:obelix-flash/shellz/watchface.hex
ssh ... 'cd obelix-flash && python3 step.py write_flash shellz/watchface.hex && \
         python3 boot_capture.py 1000000 8 | grep -E "WATCHFACE|DISPLAY_PUSH"'
```
Expected watchface UART: `WATCHFACE_UP`, `WATCHFACE_TICK HH:MM`,
`WATCHFACE_FRAME 0x..`, `DISPLAY_PUSH ok`.

## 4. Setting the displayed time (demo)

The face reads the SF32LB RTC via `kernel_wall_clock_get()`; the port formats as
UTC. To stage a known time (e.g. current Palo Alto) add a compile define — the
watchface CMakeLists has:
```
target_compile_definitions(app PRIVATE KERNEL_DEMO_EPOCH=<epoch>)
```
Compute the epoch (Palo Alto shown as UTC): `epoch = $(date +%s) - 25200` (PDT).
`wall_clock_zephyr.c` honors `#ifdef KERNEL_DEMO_EPOCH`. Rebuild `--pristine`.
NOTE: the hardcoded epoch in CMakeLists is demo-only — don't ship it.

## 5. Camera (photograph / video the panel)

The IPEVO V4K document cam points at the watch (mounted **inverted** — captures
come out 180° rotated, so rotate images 180° in post). macOS TCC blocks camera
from SSH-launched processes, so a signed **GrabD.app** (bundle id
`com.pebble.grabd`, NSCameraUsageDescription) runs in the GUI session and is
triggered over SSH via files.

Setup (one-time; re-approve the camera prompt only if the app binary is rebuilt):
```sh
# on rig, in ~/obelix-flash: build the daemon into the signed bundle ONCE
swiftc grab_daemon2.swift -o grabd2
cp grabd2 GrabD.app/Contents/MacOS/GrabD && codesign --force -s - GrabD.app
# launch it into the GUI session (uid 501) so TCC can prompt on the rig screen
sudo launchctl asuser 501 open GrabD.app          # approve the prompt once
```
The bundle (Info.plist + MacOS/GrabD) is checked in under rig/. Rebuilding the
binary changes its signature and re-prompts — so DON'T rebuild it; reuse it.

Capture:
```sh
# single still -> /tmp/watch.jpg
ssh ... 'touch /tmp/grab_request; sleep 3' ; scp -O ...:/tmp/watch.jpg .
# burst video -> /tmp/burst/NNNN.jpg at ~8fps for N seconds
ssh ... 'echo 72 > /tmp/burst_request'
```
Post-process locally (camera is inverted, so rotate 180): crop to the display
box `(875,250)+410x470` in the 1920x1080 frame, rotate 180°, then assemble with
local `ffmpeg`. (sips on the rig can pre-crop+rotate to shrink the transfer:
`sips -c 470 410 --cropOffset 250 875 -r 180 in.jpg --out out.jpg`.)

Camera gotchas:
- AVCaptureMovieFileOutput WEDGES the IPEVO — use frame capture
  (AVCaptureVideoDataOutput, what grab_daemon2 does) for video too (burst mode).
- If wedged: `sudo killall VDCAssistant appleh13camerad` then relaunch; worst
  case a physical USB replug.
- Two processes can't hold the camera — `sudo pkill -9 -f GrabD` before relaunch.

## 6. Reproduce the North Star

1. Build watchface (§1) with a fresh `KERNEL_DEMO_EPOCH` (§4).
2. Flash + verify UART (§3).
3. Start GrabD.app (§5), grab a still, rotate 180 + crop -> the watchface on the
   panel. Real Gotham, upright, PT time, "eight forty seven" style word clock.

Scripts checked in under `zephyr-port-notes/rig/`: grab_daemon2.swift (the
camera daemon), grab.swift (one-shot), grab_video.swift (mov recorder — avoid,
wedges), boot_capture.py, shell_probe.py, Info.plist (GrabD bundle plist).
