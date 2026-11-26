# PebbleOS for the Xteink X4 and X4 Pro

| Reader | Firmware board | App target | Apps |
| --- | --- | --- | --- |
| X4 | `xteink_x4` | `xteinkx4` | Native RISC-V |
| X4 Pro | `xteink_x4_pro` | `xteinkx4pro` | Native Xtensa and compatible Flint, Diorite or Aplite ARM apps through Samsa |

Both readers have a 480 × 800 physical panel and a 240 × 400 native app canvas. The [firmware releases](https://github.com/ericmigi/PebbleOS/releases) include a matched firmware and resource pair for each board, the SDK and a collection of 34 Pro watchfaces. The Android companion APK and its source require access to the [companion release](https://github.com/ericmigi/coreapp/releases/tag/xteink-companion-0.1.0).

## Flash a release over USB

Both boards install firmware over USB. PBZ over-the-air firmware updates are unavailable on these readers. CoreApp can install PBW applications over Bluetooth.

1. Download your board’s `*-pebbleos.bin`, `*-system_resources.pbpack` and `SHA256SUMS` from the **same release** into a directory such as `downloads`. Get this guide and its scripts from the matching source tag.
2. Connect the reader using a USB data cable. Close any console or other process holding that port. Run the following in the repository root and set `PORT` to the actual reader from the port listing. On Linux the device commonly appears as `/dev/ttyACM0`.

   ```sh
   python3 -m venv .venv
   . .venv/bin/activate
   python -m pip install 'esptool>=5,<6'
   python -m serial.tools.list_ports -v
   PORT=/dev/cu.usbmodemEXAMPLE
   BOARD=xteink_x4_pro                  # original X4: xteink_x4
   ```

3. Before replacing the reader’s factory firmware, save its full 16 MiB flash to a new file and keep it for recovery. `read-flash` enters the ROM download mode and reads the existing contents.

   ```sh
   python -m esptool --port "$PORT" read-flash 0 0x1000000 factory-backup.bin
   wc -c factory-backup.bin             # 16777216
   ```

4. Preview the files, checksums, chip and USB port. Run the second command to write the two images and reboot.

   ```sh
   python extras/xteink/flash.py "$BOARD" --port "$PORT" --assets downloads --dry-run
   python extras/xteink/flash.py "$BOARD" --port "$PORT" --assets downloads
   ```

   The ROM boots `pebbleos.bin` at `0x0`. System resources start at `0x300000`. This works on the first install and updates an existing PebbleOS reader without erasing its filesystem. The first PebbleOS boot initializes that filesystem and can take several minutes. To restore the factory image, use the full backup belonging to this same reader. Restoring it replaces every flash partition, including saved applications and pairing data.

   ```sh
   python -m esptool --port "$PORT" write-flash 0x0 factory-backup.bin
   ```

## Install watchfaces with a paired phone

Use this route for faces with weather, configuration, or other PebbleKit JS. CoreApp installs the native binary and resources on the watch and runs the bundled JavaScript on Android. Each face can request its own network, location or other permissions. Open its settings in CoreApp to configure services and keys it needs.

1. Install the [Xteink companion APK](https://github.com/ericmigi/coreapp/releases/tag/xteink-companion-0.1.0) and pair the reader in **Devices**. On the Pro, the connected device identifies itself as **Xteink X4 Pro**.
2. For one face, open its `.pbw` on Android with CoreApp and select the connected reader. For the collection, extract the PBWs on the computer and enable **Devices → your reader → More options → Dev Connection**. Select the correct reader if several are paired.
3. With Node.js 22.4 or newer on the computer, send the files to the phone’s developer WebSocket. Use the phone’s LAN IP on the same trusted network, or forward phone TCP port 9000 to a local port on your computer. The examples use the latter endpoint.

   ```sh
   unzip downloads/watchfaces.zip -d pro-watchfaces
   node extras/xteink/install-phone.mjs ws://127.0.0.1:9000/ pro-watchfaces/*.pbw
   ```

   For a USB-connected Android phone, create the forward before running the installer with `adb -s PHONE_SERIAL forward tcp:9000 tcp:9000` and remove it afterward with `adb -s PHONE_SERIAL forward --remove tcp:9000`. On the project’s shared lab phones use `simlock adb` through the device lease in place of `adb`. With no forwarding, substitute `ws://PHONE_LAN_IP:9000/`. The installer waits for the phone’s success status for each PBW and stops at the first failure. Rerun the remaining files after resolving it. The last successfully launched face is visible on the watch. Turn off Dev Connection and remove the forwarding rule when done.

**Original X4 with a phone.** This CoreApp revision recognizes the Pro’s native target. For an original X4 that CoreApp identifies as Flint, build an original-X4 RISC-V face, then make a reader-specific phone copy. This keeps its phone-side JavaScript in the bundle. The released Pro collection requires Pro hardware.

```sh
python3 tools/pbw_as_platform.py face.pbw face-original-x4-phone.pbw
node extras/xteink/install-phone.mjs ws://127.0.0.1:9000/ face-original-x4-phone.pbw
```

## Install over the reader’s USB console

Activate the repository’s Python environment and install its dependencies as below. Use the reader’s USB port, with the firmware running normally. The installer selects the board’s native code first and installs the matching resources and worker. The Pro also accepts compatible legacy ARM variants. Rerunning the command with the same app UUID updates its installed copy.

```sh
python tools/install_xteink_pbw.py --board xteink_x4_pro --port "$PORT" pro-watchfaces/*.pbw
python tools/install_xteink_pbw.py --board xteink_x4 --port "$PORT" original-x4-faces/*.pbw
```

USB installation handles the watch portion only. A face can draw on the reader with its companion features unavailable until its complete PBW is installed in CoreApp. For a new native original-X4 build, use its unmodified PBW with the USB installer.

## Build firmware from source

Use a full-history checkout so the firmware version and frozen SDK headers can be derived from Git history. To reproduce a release, check out its tag before initializing the pinned submodules. The branch below tracks ongoing development.

```sh
git clone --branch codex/xteink-combined https://github.com/ericmigi/PebbleOS.git
cd PebbleOS
git submodule update --init --recursive
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements.txt 'esptool>=5,<6'
```

Install the host packages and the ARM PebbleOS SDK described in the [getting-started guide](../../docs/getting_started/index.md). On macOS the host packages are also listed in `requirements-brew.txt`. Install Espressif’s RISC-V and Xtensa GCC cross-toolchains. ESP-IDF’s installer can provision both. The firmware uses these compilers directly.

```sh
git clone --branch v5.5.5 --depth 1 https://github.com/espressif/esp-idf.git ../esp-idf
../esp-idf/install.sh esp32c3,esp32s3
. ../esp-idf/export.sh
. .venv/bin/activate
riscv32-esp-elf-gcc --version
xtensa-esp32s3-elf-gcc --version

pbl configure --board xteink_x4 -b build-x4
pbl build -b build-x4
pbl configure --board xteink_x4_pro -b build-pro
pbl build -b build-pro
```

Each build directory contains `pebbleos.bin` and `system_resources.pbpack`. To flash your local build, run **one** matching command with the reader’s port. The runner reads the resource partition address from the built board description.

```sh
pbl run -b build-x4 --tty "$PORT" --resources
pbl run -b build-pro --tty "$PORT" --resources
```

## Build the SDK and a watchface

With the firmware environment active, export both native ABIs and Flint ARM from the same checkout.

```sh
pbl sdk 4.47-xteink --board xteink_x4 --board xteink_x4_pro --board qemu_flint --tarball -b build-xteink-sdk
export PEBBLE_SDK="$PWD/build-xteink-sdk/sdk-core/pebble"
```

Alternatively, extract the prebuilt SDK with `tar xzf downloads/xteink-sdk-core.tar.gz` and set `PEBBLE_SDK="$PWD/sdk-core/pebble"`. Install the SDK’s JavaScript bundler dependencies for multi-file PKJS projects. From an app project with a `CMakeLists.txt`, install its own npm packages for code, Clay or other dependencies, then build. Select the platforms declared under `pebble.targetPlatforms` in its `package.json`.

```sh
npm install --prefix "$PEBBLE_SDK/.." --ignore-scripts
cd /path/to/watchface
npm install
cmake -S . -B build -G Ninja \
  '-DPEBBLE_PLATFORMS=xteinkx4;xteinkx4pro;flint' \
  -DPEBBLE_NODE_MODULES="$PEBBLE_SDK/../node_modules"
cmake --build build
ls build/*.pbw
```

For Pro-only source select `-DPEBBLE_PLATFORMS=xteinkx4pro`. The release’s `watchface-sources.zip` contains the individual Pro projects and their build script. See [app projects](../../docs/sdk/app_build.md) and [SDK packaging](../../docs/sdk/building.md) for the platform layout, compiler selection and 144 × 168 compatibility mode.

## Build the Android companion

The [private companion source](https://github.com/ericmigi/coreapp/tree/codex/xteink-pro-companion) contains the native Pro selection, Flint catalog fallback, and Developer Connection changes. Use JDK 17 and install Android SDK Platform 37, then set `ANDROID_HOME` to your SDK path. This revision derives its Android version code from a numeric Git tag. Fetch the upstream version tag into a full-history clone.

```sh
git clone --no-tags --branch codex/xteink-pro-companion https://github.com/ericmigi/coreapp.git CoreApp-xteink
cd CoreApp-xteink
git fetch https://github.com/coredevices/CoreApp.git refs/tags/2.1.0:refs/tags/2.1.0
git submodule update --init --recursive
./gradlew :androidApp:assembleDebug
```

The APK is `public/androidApp/build/outputs/apk/debug/androidApp-debug.apk`. Android permits in-place upgrades when package name and signer match. Use the release APK to retain its signing identity on a phone with that build already installed.
