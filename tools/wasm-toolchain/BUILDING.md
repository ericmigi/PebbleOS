# How the wasm toolchain binaries were built

The binaries in `prebuilt/` were produced on 2026-08-23 from a clean Ubuntu
24.04 x86_64 container (4 CPUs, 16 GB RAM). Total wall clock: about 2.5
hours, dominated by the clang compile. `build-llvm-wasi.sh` automates all
of this; the notes below record the exact inputs and the porting issues
hit along the way, so the build can be reproduced or upgraded without
rediscovering them.

## Inputs

| Component | Version | Source |
| --- | --- | --- |
| llvm-project | 19.1.7 (release tarball) | github.com/llvm/llvm-project releases |
| wasi-sdk | 25.0 (x86_64-linux) | github.com/WebAssembly/wasi-sdk releases |
| host toolchain | Ubuntu clang 18.1.3, cmake 3.28, ninja 1.11 | apt |

## Steps

1. **Native tablegen** — LLVM's cross build needs host-runnable
   `llvm-tblgen`/`clang-tblgen`. Configure a plain native build
   (`LLVM_TARGETS_TO_BUILD=ARM`, projects `clang`) and build only the two
   tablegen targets (~15 min).
2. **Apply `patches/llvm-19.1.7-wasi-host.patch`** to the LLVM tree.
   Every hunk is a "WASI does not have this" stub — see inventory below.
3. **Cross configure** with the wasi-sdk **pthread** toolchain file
   (`wasi-sdk-pthread.cmake`, i.e. target `wasm32-wasi-threads`). The
   threads sysroot is used *only* so libc++ ships `std::mutex` & friends,
   which LLVM headers reference unconditionally; `LLVM_ENABLE_THREADS=OFF`
   keeps the runtime single-threaded (nothing ever calls
   `wasi::thread-spawn`). Key cache entries (full set in the script):
   `-DUNIX=1` (LLVM's cmake does not recognize `CMAKE_SYSTEM_NAME=WASI`),
   `LLVM_TARGETS_TO_BUILD=ARM`, `LLVM_DEFAULT_TARGET_TRIPLE=arm-none-eabi`,
   `LLVM_HOST_TRIPLE=wasm32-unknown-wasi`, `MinSizeRel`, zlib/zstd/
   libxml2/terminfo/libedit off, `_WASI_EMULATED_{MMAN,SIGNAL,
   PROCESS_CLOCKS,GETPID}` plus the matching `-lwasi-emulated-*` link
   libraries, 1 MiB shadow stack, 4 GiB max memory.
4. **`ninja clang lld`** (~2500 compile units, ~2 h on 4 cores; the two
   final wasm-ld links take several minutes each).

## Porting issues the patch covers

All in `llvm/lib/Support` (plus one in clang's driver); each stubbed
under `__wasi__`:

- `ADT/bit.h`: include `<endian.h>` (wasi-libc is musl-flavored; there is
  no `machine/endian.h`).
- `CrashRecoveryContext.cpp`: no `setjmp`/`longjmp` without the wasm
  exception-handling proposal, and no `sigaction` — crash recovery
  degrades to "run directly; a real crash traps the module".
- `LockFileManager.cpp`: no `getsid`, no `gethostname`.
- `raw_socket_stream.cpp`: no BSD sockets — implementation compiled out.
- `Unix/Unix.h`: no `<sys/wait.h>`.
- `Unix/Watchdog.inc`: no `alarm()`.
- `Unix/Signals.inc`: full stub set (no signals, no stack traces).
- `Unix/Program.inc`: `Execute`/`Wait` return "process spawning is not
  supported on WASI". The clang driver still works because cc1 runs
  in-process (the default); the linker is invoked as a separate wasm
  command by the host instead of being spawned by the driver.
- `Unix/Process.inc`: no `/dev/null`+`dup2` fixup, no signal-masked
  close, no rusage.
- `Unix/Path.inc`: no passwd database (`~user` expansion, home dir
  fallback), no `umask`, no `fcntl` file locks (single-instance sandbox:
  locking is a no-op), no `fchown`, no `posix_madvise` (declared by
  wasi-libc headers but not implemented — this one only surfaces at final
  link), `GetMainExecutable` returns argv0.
- `clang/tools/driver/cc1_main.cpp`: cmake detects `sys/resource.h` so
  `CLANG_HAVE_RLIMITS` gets defined, but wasi-libc has no `getrlimit` —
  guarded out.

## prebuilt/ contents

| File | Unpacked | sha256 (unpacked) |
| --- | --- | --- |
| `clang.wasm.xz` | 90 MB | `2566a16f0e83394811d106b17e578c1084b80844d3cc68e1f2fd2dc5e90dd562` |
| `lld.wasm.xz` | 55 MB | `2154b7afd12bca37d2c04932db09ead8000404b0bc972a0dd3c451eca5f57c43` |
| `clang-res.tar.xz` | clang builtin headers (`lib/clang/19` from the build) | — |

Binaries are stored xz-compressed to stay well under GitHub's file-size
limits. Unpack in place:

```sh
cd tools/wasm-toolchain/prebuilt
unxz -k clang.wasm.xz lld.wasm.xz
tar xJf clang-res.tar.xz
```

That yields the layout `build-app-wasm.mjs` expects for `--toolchain`:
`clang.wasm`, `lld.wasm`, `clang-res/`. Both modules are
`wasm32-wasi-threads` commands: the host must provide an imported shared
memory and a (never-called) `wasi::thread-spawn` stub — `run-wasm-tool.mjs`
shows the ~50 lines needed. Quick smoke test:

```sh
node ../run-wasm-tool.mjs clang.wasm --argv0 clang -- --print-targets
# expect: arm / armeb / thumb / thumbeb
```

## Known wrinkle

One `lld.wasm` link invocation trapped mid-run and succeeded identically
on retry (suspected memory-growth edge in the Node WASI host). If a link
traps, retry before debugging.
