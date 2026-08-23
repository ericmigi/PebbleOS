#!/bin/bash
# Build clang + ld.lld as WebAssembly (WASI) binaries with the ARM backend,
# suitable for compiling Pebble SDK apps in a browser or any wasm runtime.
#
# Produces: $WORK/build-wasi/bin/clang.wasm and lld.wasm (wasm32-wasi-threads).
#
# Requirements on the host: cmake, ninja, a native C++ toolchain, curl, ~20 GB
# of disk and a few hours of CPU time.
set -euo pipefail

WORK="${1:-$PWD/wasm-toolchain-build}"
LLVM_VERSION=19.1.7
WASI_SDK_VERSION=25.0
WASI_SDK_TAG=wasi-sdk-25
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

mkdir -p "$WORK"
cd "$WORK"

# --- fetch toolchain + sources ---------------------------------------------
if [ ! -d "wasi-sdk-${WASI_SDK_VERSION}-x86_64-linux" ]; then
    curl -sSL -o wasi-sdk.tar.gz \
        "https://github.com/WebAssembly/wasi-sdk/releases/download/${WASI_SDK_TAG}/wasi-sdk-${WASI_SDK_VERSION}-x86_64-linux.tar.gz"
    tar xzf wasi-sdk.tar.gz
fi
WASI_SDK="$WORK/wasi-sdk-${WASI_SDK_VERSION}-x86_64-linux"

if [ ! -d llvm ]; then
    curl -sSL -o llvm-project.tar.xz \
        "https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/llvm-project-${LLVM_VERSION}.src.tar.xz"
    tar xf llvm-project.tar.xz
    mv "llvm-project-${LLVM_VERSION}.src" llvm
    # WASI has no processes, signals, sockets, or passwd database: stub those
    # paths out of LLVMSupport.
    patch -p1 -d llvm < "$SCRIPT_DIR/patches/llvm-${LLVM_VERSION}-wasi-host.patch"
fi

# --- native tablegen (runs on the build host during the cross build) --------
if [ ! -x build-native/bin/llvm-tblgen ]; then
    cmake -G Ninja -S llvm/llvm -B build-native \
        -DCMAKE_BUILD_TYPE=Release \
        -DLLVM_ENABLE_PROJECTS="clang" \
        -DLLVM_TARGETS_TO_BUILD=ARM \
        -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF \
        -DLLVM_INCLUDE_EXAMPLES=OFF
    ninja -C build-native llvm-tblgen clang-tblgen
fi

# --- cross build to wasm32-wasi-threads -------------------------------------
# The -threads sysroot is used so libc++ ships std::mutex & friends (LLVM
# headers use them unconditionally); LLVM_ENABLE_THREADS=OFF means nothing
# actually spawns threads at runtime.
EMULATED="-D_WASI_EMULATED_MMAN -D_WASI_EMULATED_SIGNAL -D_WASI_EMULATED_PROCESS_CLOCKS -D_WASI_EMULATED_GETPID"
cmake -G Ninja -S llvm/llvm -B build-wasi \
    -DCMAKE_TOOLCHAIN_FILE="$WASI_SDK/share/cmake/wasi-sdk-pthread.cmake" \
    -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DUNIX=1 \
    -DLLVM_ENABLE_PROJECTS="clang;lld" \
    -DLLVM_TARGETS_TO_BUILD=ARM \
    -DLLVM_DEFAULT_TARGET_TRIPLE=arm-none-eabi \
    -DLLVM_HOST_TRIPLE=wasm32-unknown-wasi \
    -DLLVM_NATIVE_TOOL_DIR="$WORK/build-native/bin" \
    -DLLVM_ENABLE_THREADS=OFF -DLLVM_ENABLE_PIC=OFF \
    -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF \
    -DLLVM_INCLUDE_BENCHMARKS=OFF \
    -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF \
    -DLLVM_ENABLE_TERMINFO=OFF -DLLVM_ENABLE_LIBEDIT=OFF \
    -DLLVM_ENABLE_BACKTRACES=OFF -DLLVM_ENABLE_CRASH_OVERRIDES=OFF \
    -DLLVM_ENABLE_BINDINGS=OFF \
    -DCLANG_ENABLE_ARCMT=OFF -DCLANG_ENABLE_STATIC_ANALYZER=OFF \
    -DCMAKE_C_FLAGS="$EMULATED" \
    -DCMAKE_CXX_FLAGS="$EMULATED" \
    -DCMAKE_EXE_LINKER_FLAGS="-lwasi-emulated-mman -lwasi-emulated-signal -lwasi-emulated-process-clocks -lwasi-emulated-getpid -Wl,-z,stack-size=1048576 -Wl,--max-memory=4294967296"
ninja -C build-wasi clang lld

echo "Done: $WORK/build-wasi/bin/clang and lld (wasm32-wasi-threads modules)"
