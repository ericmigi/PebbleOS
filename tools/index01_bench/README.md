# Index 01 ring scan: power and latency study

Research notes for the always-on Index 01 ring scan in `subsys/bluetooth/shell.c`
(branch `eric/index01-ring-scan`). The watch passively scans for the ring's
advertisements and runs the vibration motor while the ring reports its button
held (RECORDING flag). The question: how little can the watch listen and still
pick up a press quickly enough to feel instant?

Measured November 2026 on the unicorn-mac-3 rig: obelix DVT watch, release
firmware, powered and measured at 3.8 V by a Nordic PPK2, phone (Pixel)
connected over BLE the whole time.

## Result

| | Value |
|---|---|
| Recommended setting | 60 ms window every 480 ms, passive, duplicate filter off (12.5% duty) |
| Idle current with that setting | 771–856 µA (3 runs) |
| Idle current, main (no scan) | ~153 µA (120–205 µA across runs) |
| Idle current, original proof of concept (30/30 ms, 100% duty) | 3,610 µA |
| Press latency, 42 fake presses at 50 ms adverts, 0 missed | p50 ~140–250 ms, p90 ~300–480 ms, max 851 ms |
| Release latency | p50 ~285–320 ms, p90 ~470–550 ms |

So the scan costs about 650 µA on top of main, down from about 3,450 µA.

- **If latency matters more than power:** use 60 ms every 240 ms (25% duty).
  It draws 1,290 µA and gives press p90 231 ms and release p90 204 ms.
- **If power matters more:** use 60 ms every 600 ms. It draws 759 µA, but press
  p90 is 554 ms and the max is 1.1 s.

All of these numbers assume the ring advertises every 50 ms while the button is
held. **That is still unverified for the real ring** (see "Open questions"). At
100 ms adverts the same 480/60 setting doubles its latency (p90 1,075 ms). The
final pick depends on that one number.

The setting defaults in `shell.c` are `INDEX01_SCAN_ITVL_MS 480`,
`INDEX01_SCAN_WIN_MS 60`, `INDEX01_SCAN_FILTER_DUPS 0` and
`INDEX01_SCAN_PASSIVE 1`.

## Setup

- **Watch:** obelix DVT (the lab file calls it `obelix@pvt`). Release build
  `obelix@dvt` with `CONFIG_RELEASE`, flashed with unicorn's `step.py`. A
  release build has no console, so the scan starts itself from the NimBLE
  sync callback (`pbl_bt_index01_scan_start()` in `init.c`).
- **Power:** PPK2 in source mode at 3.8 V, driven only through `pbl power`
  (`~/pbl/bin/pbl`, installed by unicorn's `qa-release/bin/install-pbl.sh`):
  - `pbl power measure --seconds N --json` for idle;
  - `pbl power start` / `stop --save DIR` to keep a 1 ms trace of the press
    phase.
- **Fake ring:** nRF52840 USB dongle (PCA10059) running Zephyr `hci_uart`
  (`/dev/cu.usbmodem114201`, 1 Mbaud), driven by Bumble 0.0.236.
  - It advertises exactly what the ring sends: flags; the 128-bit service UUID
    `607B5C9B-3700-4E94-F44A-2DF900BCB0C3`; and manufacturer data `0x0EEA` +
    fingerprint (4) + count (1) + flags (1), with RECORDING = `0x20`.
  - The scan response holds the name `Pebble Index 071`.
  - It uses the static random address `C4:15:0E:01:00:71`.
  - A second dongle (in DFU bootloader) is unused.
- **Phone:** Pixel `33141JEHN24410` on adb. The bench waits for
  `dumpsys bluetooth_manager` to show the watch connected after power-on.
- **Real ring:** Index 01 "Pebble Index 071". It is silent when idle and
  advertises only around button events.

## Method

Each configuration is one firmware build:
1. `build_cfg.sh NAME ITVL WIN DUPS PASSIVE` rewrites the four defines, builds
   on the laptop, and copies the build to the rig. Never build on the rig.
2. `run_cfg.sh NAME` flashes it (three retries per image).
3. `index_bench.py` then runs the steps below.

1. **Power-cycle the watch** with `pbl power off`, 1.5 s off, then `on` at
   3800 mV. Wait for the phone to reconnect (7–10 s), then let it settle for
   60 s.
2. **Measure idle current** for 60 s, with nothing pressed. This is the power
   number.
3. **Fake presses.** The dongle advertises RECORDING for 2 s, then the cleared
   flags for 3 s, then stays silent for a random 4–9 s gap. This repeats 12, 16
   or 30 times, with the RNG seeded by the config name. The PPK2 trace runs
   during this phase but is used only for timing, never for power.
4. **Latencies.**
   - DOWN is the time from the dongle's start-advertising call to the motor's
     first 10 ms bin.
   - UP is the time from the release advert to the motor stopping.
   - Both clocks are host `time.monotonic()`; the trace starts at a known
     offset.
   - A press with no motor run inside its window counts as missed.
5. **Results** go to `results/NAME.json`. Logs are in `logs/sweep-*.log`. The
   1 ms traces (`*-trace.npz`, 12 MB) stay on the rig in
   `~/pbl-power/scan-results/`.

The "vibrate on DOWN, stop on UP" behaviour doubles as the detector. The motor
draws about 22 mA over baseline, so it shows clearly in the PPK2 trace with no
firmware instrumentation and no console. It also means:

- **Never measure power while the motor runs.** One 2 s buzz per 10 s would
  swamp the radio's hundreds of µA (a press phase averages about 4.7 mA). Idle
  and presses are separate phases for that reason.

## Test plan: narrowing in

The target I set for "feels instant":
- no missed presses;
- press p90 at or under 300 ms, with no press over 600 ms;
- release p90 at or under 500 ms.

Release latency matters less, because the motor's 10 s step auto-stops anyway.

The plan:
- **Stage A, scan mode at full duty:** find which knobs other than duty cycle
  cost power.
- **Stage B, duty cycle (window fixed at 30 ms, interval varied):** find the
  power curve and where latency breaks down.
- **Stage C, longer windows:** test the hypothesis from B that the window must
  cover the ring's advert interval. Then sweep the interval at those windows.
- **Stage C2, refine** around the best point from C with more presses (16).
- **Stage D, confirm** the winner with 30 presses, and test how sensitive it is
  to the ring's advert interval (50 vs 100 ms).
- **Stage E, real ring:** time real presses and measure the real advert
  interval. Not done: the ring was never seen (see "Open questions").

### Stage A: scan mode at 100% duty (30 ms window every 30 ms), 12 presses

| Config | Idle µA | Press p50 / p90 / max (ms) | Release p50 / p90 (ms) |
|---|---|---|---|
| active, no duplicate filter (original PoC) | 3,610 | 24 / 34 / 81 | 38 / 48 |
| active, duplicate filter | 3,560 | 22 / 79 / 79 | 167 / 201 |
| passive, no duplicate filter | 3,613 | 22 / 29 / 34 | 41 / 105 |
| passive, duplicate filter | 3,556 | 19 / 78 / 80 | 118 / 199 |

- **Scan mode and the duplicate filter don't move power:** they change it by
  1.5% or less. The cost is the receiver being on.
- **The duplicate filter hurts release detection:** release p50 goes from about
  40 ms to 120–170 ms. The controller filters repeat adverts from the same
  address, so the flag change can be dropped for a while. Keep it off.
- **Passive scanning is as fast as active**, because detection uses the advert
  itself, not the scan response. It also sends no SCAN_REQ. The catch: a
  passive watch never receives the scan response, so the ring's name is never
  logged. That is harmless, because matching uses the UUID.

### Stage B: duty cycle (30 ms window), 12 presses

| Interval (duty) | Idle µA | Press p50 / p90 / max (ms) | Release p50 / p90 (ms) | Missed |
|---|---|---|---|---|
| 60 ms (50%) | 2,153 | 70 / 223 / 250 | 174 / 350 | 0 |
| 120 ms (25%) | 1,304 | 142 / 415 / 467 | 141 / 407 | 0 |
| 240 ms (12.5%) | 863 | 216 / 507 / 1,058 | 419 / 586 | 0 |
| 480 ms (6.25%) | 636 | 404 / 1,352 / 1,825 | 748 / 2,233 | 0 |
| 960 ms (3.1%) | 465 | 889 / 1,372 / 1,585 | 806 / 2,599 | 3 / 12 |

- **Power is roughly linear in duty.**
- **Latency grows much faster than the interval:** at 240 and 480 ms the p90 is
  about 2–3 times the interval. A 30 ms window is shorter than the ring's 50 ms
  advert interval plus its 0–10 ms random advDelay. Many windows therefore
  fall between two adverts and catch nothing, so a press has to wait several
  intervals.

### Stage C: window at least the advert interval, 12 presses

| Window / interval (duty) | Idle µA | Press p50 / p90 / max (ms) | Release p50 / p90 (ms) |
|---|---|---|---|
| 60 / 240 ms (25%) | 1,290 | 141 / 231 / 513 | 153 / 204 |
| 60 / 480 ms (12.5%) | 856 | 139 / 308 / 470 | 321 / 469 |
| 60 / 960 ms (6.25%) | 518 | 586 / 926 / 1,239 | 566 / 904 |
| 100 / 480 ms (21%) | 1,115 | 138 / 394 / 415 | 200 / 363 |
| 100 / 960 ms (10.4%) | 777 | 321 / 527 / 1,082 | 429 / 1,057 |

- **Hypothesis confirmed.** At the same 12.5% duty, 60/480 beats 30/240 at the
  same power. The max drops from 1,058 to 470 ms, and the p90 from 507 to 308 ms.
- **A 60 ms window catches an advert in most windows,** so most presses land
  within about one interval. It isn't a hard bound: stage D's 30 presses reach
  851 ms at 480 ms intervals.
- **100 ms windows** tighten the tail further, at more power.

### Stage C2: refine around 60–70 ms windows, 16 presses

| Window / interval (duty) | Idle µA | Press p50 / p90 / max (ms) | Release p50 / p90 (ms) |
|---|---|---|---|
| 60 / 600 ms (10%) | 759 | 241 / 554 / 1,122 | 425 / 727 |
| 60 / 720 ms (8.3%) | 709 | 417 / 882 / 1,197 | 446 / 637 |
| 70 / 480 ms (14.6%) | 936 | 326 / 452 / 688 | 317 / 436 |
| 70 / 600 ms (11.7%) | 827 | 382 / 519 / 570 | 268 / 479 |

- **Past 480 ms the tail grows faster than power falls.**
- **70 ms windows** buy a tighter max (570 ms at 600) for the same power as
  60/480.

### Stage D: confirm 60/480 with 30 presses, at two ring advert intervals

| Ring advert interval | Idle µA | Press p50 / p90 / max (ms) | Release p50 / p90 (ms) | Missed |
|---|---|---|---|---|
| 50 ms | 771 | 247 / 484 / 851 | 285 / 553 | 0 / 30 |
| 100 ms | 792 | 446 / 1,075 / 1,493 | 462 / 1,113 | 0 / 30 |

- **At 50 ms adverts, 60/480 is solid but over the 300 ms p90 target.** The
  12-press run in stage C was optimistic.
- **The ring's advert interval is the main lever on latency, so it has to be
  measured.** If the real ring advertises at 100 ms or slower:
  - the window needs to be at least 110 ms to keep catching an advert in most
    windows;
  - or the interval should drop to 240 ms.

### Power model

A least-squares fit over the 20 fake-ring runs (one point per run) gives:

> idle ≈ 439 µA + 3,169 µA × duty

- **Each 1% of duty costs about 32 µA.**
- **It fits within about 8% from 8% to 100% duty.** Below that it overshoots:
  960/60 (6.25%) measured 518 µA against 637 predicted, and 960/30 (3.1%)
  measured 465 µA against 538.
- **Same-duty pairs agree within run-to-run noise** (about ±50 µA; 60/480 gave
  771 to 856 µA across three runs). For example, 30/240 gave 863 µA and 60/480
  gave 856 µA, so a per-window cost can't be resolved from this data.

### Why it costs so much even though the scan is passive

"Passive" only means the watch never transmits a scan request. The cost is the
receiver being on, and that's the same for active and passive scanning (stage
A: 3,556 to 3,613 µA either way).

- **Listening at 100% duty adds about 3.45 mA over main.** That is radio
  receive current plus the BLE controller staying awake, which is typical for a
  BLE receiver.
- **There also seems to be a floor of a few hundred µA once a scan is on at
  all.** 960/30 (3.1% duty) costs 312 µA over main, where 3.1% of 3.45 mA would
  be about 107 µA.
  - Cost per unit of duty rises as duty falls: about 3.4 mA per 100% at full
    duty, but 5–10 mA per 100% at 3–6%.
  - That points to a fixed or per-window overhead on top of the listening
    itself.
- **Candidates for that overhead** (none measured):
  - the BLE core or its clock waking from deep sleep for every window;
  - scanning blocking the deepest sleep state altogether;
  - the host CPU waking for every advert report. With the duplicate filter
    off, every advert from every nearby device is delivered to the host, and
    the lab air is busy (about 32 adverts a second seen by the dongle).
    However, turning the duplicate filter on at 100% duty saved only about
    50 µA, so this is probably a small part.
- **Ways to measure the floor:**
  - a near-zero-duty scan (for example a 2.5 ms window every 10 s);
  - the same run in RF-quiet surroundings;
  - controller-side filtering (filter accept list or vendor filter) so only the
    ring's adverts reach the host.

### Latency model

This is an idealised model, not a measured guarantee.

- **When a window should catch the ring:** the ring sends each advert on all
  three channels within about 1 ms, so the scanner's channel doesn't matter,
  only whether it is listening. In the ideal case, a window W of at least the
  advert interval A plus 10 ms catches an advert every time. Press latency is
  then roughly uniform on [0, I], plus processing and motor start-up.
- **The measured tails are longer than that.** 60/480 reaches 851 ms and 60/600
  reaches 1,122 ms. Possible causes, none confirmed:
  - a 60 ms window is only just above 50 ms + 10 ms of advDelay;
  - the controller drops reports or schedules the scan around the phone
    connection's events;
  - an advert caught in a window can be lost to noise.
- **What happens when it isn't:** if W is under A + 10 ms, each window catches
  an advert with probability of about W/A. Latency then has a geometric tail in
  multiples of I. The 30 ms runs in stage B show this.

## Lessons learned (measurement)

- **The PPK2 throws away samples the host doesn't read in time.** It streams
  100 kS/s of 4-byte samples. The old ppk2d polled with sleeps and covered
  4% of samples over ssh and 30% under launchd. Its averages were wrong, and
  they hid the scan drain.
  - `pbl power` (from `tools/libs/pbl-cli`) does blocking reads and is correct.
  - Unicorn moved to it (unicorn `a3ff0d7`: `Ppk2Power`, `step.py` and the
    scripts now call `pbl power`; ppk2d is retired).
- **The PPK2 cuts power when its controlling process exits.** Keep the
  `pbl power` daemon alive with `pbl power on`, and release it with
  `pbl power off`.
- **`pbl` only runs inside a PebbleOS checkout** (it needs `pbl.yml`). The rig
  install keeps a sparse checkout in `~/pbl/pebbleos`, and the wrapper
  `~/pbl/bin/pbl` runs there.
- **On macOS, ssh sessions have no TMPDIR, but launchd jobs (the CI runner)
  have one.** `pbl power` keeps its locks and socket in the temp dir, so the
  two contexts end up with separate daemons on the same PPK2. The wrapper pins
  TMPDIR with `getconf DARWIN_USER_TEMP_DIR`.
- **Watch the phone link before trusting idle numbers.** A lost bond (Pixel
  `KEY_MISSING`, bond-loss reason 3) makes the phone retry about every 3 s,
  which adds about 1 mA. Re-pair, then confirm a single CONNECTED in
  `dumpsys bluetooth_manager` before measuring.
- **Detect the motor on 10 ms means, not 1 ms ones.** 1 ms means ripple below
  any useful threshold. 10 ms means above 14 mA held for at least 5 bins, with
  gaps under 50 ms merged, found every motor run with no false positives.
  (`index_bench.py` now merges gaps before applying the 50 ms minimum; the
  checked-in results used the older order, which only matters for bursts
  under 50 ms.)
- **Bracket the recorder's start and stop to align clocks.** `pbl power start`
  gives no timestamp for its first sample. The bench times both the start and
  stop calls, then intersects those windows using the trace's length, and
  records the result's uncertainty as `clock_uncertainty_ms`. The checked-in
  results took the time just after `start` returned, so their latencies may
  run long by up to that call's duration.
- **Never mix the vibration phase into a power number.** Measure idle on its
  own, with nothing pressed.
- **Release builds print no build ID on the console,** so a flash can't be
  confirmed from boot logs. Check the flash tool's exit code
  (`STEP_EXIT 0`) instead.
- **Bumble with an HCI dongle is a good fake peripheral.** The same dongle
  plays the ring and sniffs for it.
  - Only one process may open the dongle. A stale `ring_sniff.py` left running
    made the next one hang forever on `HCI_Reset` with "transport lost".
  - Kill stale sniffers, and give long sniffs a kill timer.
- **The rig is shared with CI.** Check for `Runner.Worker` and which lane it
  serves before taking the watch or PPK2. The vphone iOS lane doesn't touch
  the watch.

## Open questions

- **How often does the real ring advertise while the button is held?** This
  decides between 60/480 and a longer window or a shorter interval.
  - Three attempts saw no Index 01 adverts at all:
    - `index_bench.py --real`;
    - a 10-minute `ring_sniff.py` that hung;
    - a 10-minute `ring_sniff.py` that saw 19,516 other adverts.
  - Either the ring wasn't pressed in those windows, or it's out of range of
    the dongle, or it only advertises while it's not connected to the phone.
  - Next attempt: sniff near the ring with someone pressing it on cue.
- **Does the ring rotate its random address?** The firmware handles it by
  reusing slots, but this hasn't been seen in practice.
- **Where the few-hundred-µA scan floor comes from** (see "Why it costs so much
  even though the scan is passive").
- **Ring-side changes are the bigger lever.** If the ring advertised every
  20 ms for its first second after a press, a 30 ms window would catch it, at
  roughly half the duty for the same latency.
- **Adaptive scanning** (for example 100% duty for a few seconds after a press,
  or while the screen is on) wasn't tried.

## Reproducing

From the laptop, with the worktree configured as a release `obelix@dvt` build in
`build-obelix-dvt-rel-index`:

```
tools/index01_bench/build_cfg.sh i480w60pn 480 60 0 1
```

On the rig (copy `index_bench.py`, `ring_sniff.py`, `run_cfg.sh` and `sweep.sh`
to `~/pbl-power/`, a venv with bumble and numpy):

```
~/pbl-power/sweep.sh D i480w60pn -- --presses 30
~/pbl-power/run_cfg.sh i480w60pn --presses 30 --adv-ms 100
venv/bin/python ring_sniff.py serial:/dev/cu.usbmodem114201,1000000 600
```

`index_bench.py --real N --no-power-cycle --keep-on` times N real ring presses
on whatever firmware is already running.

Paths in the scripts are this rig's: the ssh host in `build_cfg.sh`, the dongle
port, the phone serial and `step.py`'s location. Change them for another rig.

## Files

- `index_bench.py`: the fake ring, plus the power and latency bench.
- `ring_sniff.py`: logs every Index 01 advert with a timestamp.
- `build_cfg.sh`: builds one setting and copies it to the rig.
- `run_cfg.sh`: flashes and benches one setting.
- `sweep.sh`: runs settings in turn, stops if a CI job takes the rig, and
  exits non-zero if any run failed.
- `test_index_bench.py`: checks the trace analysis (motor runs, clock origin,
  matching presses), with no rig needed:
  `uv run --no-project --with numpy --with bumble python test_index_bench.py`.
- `results/*.json`: one result per run, with per-press latencies. The names
  read `i<interval>w<window>` + `a`/`p` (active/passive) + `n`/`d`
  (duplicate filter off/on). `D-*` is stage D; `D-real-*` is the empty
  real-ring run.
- `logs/sweep-*.log`: the sweep logs for each stage.
