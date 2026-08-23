# WebAssembly SDK toolchain (proof of concept)

Compile Pebble SDK apps with a toolchain that runs as WebAssembly — the
building block for compiling apps directly in a browser (CloudPebble without
build servers).

## Why clang instead of arm-none-eabi-gcc

GCC has no wasm host port (its driver relies on spawning `cc1`/`as`/`ld`
subprocesses, which WASI cannot do). LLVM builds cleanly for
`wasm32-wasi-threads`, its ARM backend emits Cortex-M code, clang's `cc1`
runs in-process, and `ld.lld` consumes the SDK's `pebble_app.ld` linker
script unchanged.

## What was validated

Using the `pebble-timer` app against an SDK exported from this repo
(`qemu_gabbro` board):

1. **Native clang+lld** (`arm-none-eabi-gcc-shim`): the stock SDK waf
   pipeline builds a `.pbw` with clang 18 + ld.lld instead of gcc. The app
   installs and runs correctly in the QEMU emulator (UI, fonts, resources,
   timers all functional).
2. **Loader compatibility**: `--emit-relocs` + `--build-id=sha1` +
   `pebble_app.ld` all work identically under lld; `inject_metadata.py`
   parses the lld ELF without changes. The lld-linked app contains *zero*
   absolute relocations (gcc builds carry ~272, nearly all from newlib
   malloc internals the gcc driver links implicitly), so the PIE loader has
   nothing to patch.
3. **Toolchain as wasm** (in progress): clang + ld.lld cross-compile to
   `wasm32-wasi-threads` (ARM backend only) with the patch below; the
   end-to-end app build under Node's WASI host is driven by
   `build-app-wasm.mjs`.

## Files

- `build-llvm-wasi.sh` — builds `clang.wasm` / `lld.wasm` from LLVM 19.1.7
  with wasi-sdk 25. Takes a few hours; artifacts are ~a hundred MB and
  cacheable forever.
- `patches/llvm-19.1.7-wasi-host.patch` — teaches LLVM's Unix support layer
  that WASI has no processes, signals, sockets, file locks, or passwd
  database. All stubs; no behavior change on other hosts.
- `run-wasm-tool.mjs` — Node WASI host for one tool invocation (handles the
  shared-memory import of `-threads` modules; thread spawning is stubbed —
  LLVM is built with `LLVM_ENABLE_THREADS=OFF`).
- `build-app-wasm.mjs` — compiles an app's sources with `clang.wasm` and
  links with `lld.wasm`.
- `package-app.py` — replays the SDK's post-link steps (objcopy, metadata
  injection, `.pbw` bundling) for an ELF built outside waf.
- `arm-none-eabi-gcc-shim` — drop-in gcc replacement using native clang+lld,
  for validating the LLVM path against the stock waf pipeline.

## End-to-end recipe

```sh
# One-time: SDK + golden build (generates appinfo.auto.c, resources, JS)
./pbl configure --board qemu_gabbro && ./pbl build
cd $APP && $PEBBLEOS/build/sdk/waf configure build

# Compile + link with the wasm toolchain
node tools/wasm-toolchain/build-app-wasm.mjs \
  --toolchain $TOOLCHAIN_DIR \
  --sdk $PEBBLEOS/build/sdk/gabbro \
  --newlib /usr/include/newlib \
  --libgcc "$(arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -print-libgcc-file-name)" \
  --libc "$(arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -print-file-name=libc.a)" \
  --ldscript $APP/build/gabbro/pebble_app.ld.auto \
  --out $APP/build/gabbro/pebble-app-wasm.elf \
  $APP/src/*.c $APP/build/gabbro/appinfo.auto.c \
  $APP/build/gabbro/src/resource_ids.auto.c $APP/build/src/message_keys.auto.c

# Package and install
python3 tools/wasm-toolchain/package-app.py \
  --sdk-tools $PEBBLEOS/build/sdk/common/tools \
  --build $APP/build --platform gabbro \
  --elf $APP/build/gabbro/pebble-app-wasm.elf --out timer.pbw
```

## App-side compiler differences (clang vs gcc)

- `pebble.h` re-declares some typedefs; clang at `-std=c99` treats that as
  an error (C11 feature) — needs `-Wno-typedef-redefinition`.
- clang's `-Wextra` enables `-Wmissing-field-initializers` for `{ NULL }`
  initializers that gcc accepts silently.
- Apps are linked `-nostdlib`; `__aeabi_*` helpers come from `libgcc.a` +
  newlib `libc.a` (bundled as plain data files for the wasm toolchain).

## Toward the browser

The remaining pieces are host plumbing, not compiler work:

- **WASI in the browser**: run the same two modules against a WASI polyfill
  (e.g. `browser_wasi_shim`) over an in-memory FS holding the SDK
  `include/`, `lib/libpebble.a`, newlib headers, `libgcc.a`/`libc.a`, and
  the linker script (~a few MB of static assets).
- **Shared-memory note**: `-threads` modules need cross-origin isolation
  (COOP/COEP headers, or the `coi-serviceworker` shim on static hosts).
  Alternatively rebuild against the plain `wasm32-wasip1` sysroot with a
  handful more single-thread patches to drop the requirement.
- **Python steps**: `inject_metadata.py`, resource compilation, and
  `mkbundle.py` are pure Python (+Pillow for images) and run under Pyodide;
  `.pbw` zipping can also be done in JS directly.
- **objcopy**: the ELF→raw-binary step (`llvm-objcopy` can be added to the
  same wasm build; the current PoC uses the native binutils objcopy).
- **Running the result**: QEMU has wasm ports; long-term even the emulator
  could run client-side.
