#!/usr/bin/env node
// Run a WASI-compiled LLVM tool (clang / ld.lld) under Node's WASI host.
//
// Usage: node run-wasm-tool.mjs <tool.wasm> --argv0 <name> \
//          [--mapdir GUEST=HOST]... -- <argv...>
//
// The tools are wasm32-wasi-threads modules: they import shared linear
// memory, which the host must create. Nothing spawns threads at runtime
// (LLVM is built with LLVM_ENABLE_THREADS=OFF), so a stub thread-spawn
// import is sufficient. In a browser the same modules run against a WASI
// polyfill (e.g. browser_wasi_shim) with an in-memory filesystem; shared
// wasm memory needs cross-origin isolation (COOP/COEP).
import { readFileSync } from 'node:fs';
import { WASI } from 'node:wasi';

const args = process.argv.slice(2);
const wasmPath = args.shift();
const preopens = {};
let argv0 = wasmPath.split('/').pop().replace(/\.wasm$/, '');
let rest = [];
for (let i = 0; i < args.length; i++) {
  if (args[i] === '--mapdir') {
    const [guest, host] = args[++i].split('=');
    preopens[guest] = host;
  } else if (args[i] === '--argv0') {
    argv0 = args[++i];
  } else if (args[i] === '--') {
    rest = args.slice(i + 1);
    break;
  }
}

const wasi = new WASI({
  version: 'preview1',
  args: [argv0, ...rest],
  env: {},
  preopens,
  returnOnExit: true,
});

const wasm = await WebAssembly.compile(readFileSync(wasmPath));
const imports = wasi.getImportObject();

for (const im of WebAssembly.Module.imports(wasm)) {
  if (im.module === 'env' && im.name === 'memory' && im.kind === 'memory') {
    imports.env = {
      ...(imports.env || {}),
      memory: new WebAssembly.Memory({
        initial: 1088, maximum: 65536, shared: true,
      }),
    };
  }
  if (im.module === 'wasi' && im.name === 'thread-spawn') {
    imports.wasi = { 'thread-spawn': () => -1 };
  }
}

const instance = await WebAssembly.instantiate(wasm, imports);
const code = wasi.start(instance);
process.exit(code ?? 0);
