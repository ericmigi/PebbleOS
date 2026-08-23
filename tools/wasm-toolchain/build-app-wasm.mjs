#!/usr/bin/env node
// Build a Pebble app's ELF entirely with the WebAssembly toolchain:
// every .c is compiled by clang.wasm and the ELF is linked by lld.wasm,
// with no native compiler involved. Metadata injection and .pbw bundling
// stay in the (Pyodide-portable) Python SDK tools.
//
// Usage:
//   node build-app-wasm.mjs --toolchain <dir with clang.wasm+lld.wasm> \
//     --sdk <build/sdk/PLATFORM> --newlib <newlib include dir> \
//     --libgcc <libgcc.a> --libc <libc.a> \
//     --ldscript <pebble_app.ld.auto> --out <out.elf> \
//     <src.c|obj.o>... [-- <extra cflags>]
//
// Sources are compiled relative to the current directory; generated files
// (appinfo.auto.c, resource_ids.auto.c, message_keys.auto.c) from a prior
// `waf configure` run are accepted as inputs like any other source.
import { spawnSync } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { basename, dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const opt = { srcs: [], extra: [] };
{
  const a = process.argv.slice(2);
  for (let i = 0; i < a.length; i++) {
    switch (a[i]) {
      case '--toolchain': opt.toolchain = resolve(a[++i]); break;
      case '--sdk': opt.sdk = resolve(a[++i]); break;
      case '--newlib': opt.newlib = resolve(a[++i]); break;
      case '--libgcc': opt.libgcc = resolve(a[++i]); break;
      case '--libc': opt.libc = resolve(a[++i]); break;
      case '--ldscript': opt.ldscript = resolve(a[++i]); break;
      case '--out': opt.out = resolve(a[++i]); break;
      case '--': opt.extra = a.slice(i + 1); i = a.length; break;
      default: opt.srcs.push(resolve(a[i]));
    }
  }
}
for (const k of ['toolchain', 'sdk', 'newlib', 'ldscript', 'out']) {
  if (!opt[k]) { console.error(`missing --${k}`); process.exit(2); }
}

function runTool(wasm, argv0, mapdirs, argv) {
  const args = [join(here, 'run-wasm-tool.mjs'), join(opt.toolchain, wasm),
                '--argv0', argv0];
  for (const [g, h] of Object.entries(mapdirs)) args.push('--mapdir', `${g}=${h}`);
  args.push('--', ...argv);
  const r = spawnSync(process.execPath, args, { stdio: 'inherit' });
  return r.status ?? 1;
}

const CFLAGS = [
  '--target=arm-none-eabi', '-mcpu=cortex-m3', '-mthumb',
  '-std=c99', '-ffunction-sections', '-fdata-sections', '-fcommon',
  '-g', '-fPIE', '-Os', '-D_TIME_H_', '-Dtime_t=long',
  '-Wall', '-Wno-typedef-redefinition', '-Wno-missing-field-initializers',
  '-isystem', '/newlib', '-I', '/sdk/include',
];

const objs = [];
const objdir = mkdtempSync(join(tmpdir(), 'pblwasm-'));
let failed = false;
for (const src of opt.srcs) {
  if (src.endsWith('.o')) { objs.push(src); continue; }
  const obj = join(objdir, basename(src) + '.o');
  console.log(`[clang.wasm] ${basename(src)}`);
  const status = runTool('clang.wasm', 'clang', {
    '/newlib': opt.newlib,
    '/sdk': opt.sdk,
    '/src': dirname(src),
    '/obj': objdir,
    // Project-relative includes (../src style) resolve inside /src.
  }, [...CFLAGS, ...opt.extra,
      '-c', `/src/${basename(src)}`, '-o', `/obj/${basename(obj)}`]);
  if (status !== 0) { failed = true; break; }
  objs.push(obj);
}

if (!failed) {
  console.log('[lld.wasm] linking', basename(opt.out));
  const mapdirs = {
    '/sdk': opt.sdk,
    '/obj': objdir,
    '/ld': dirname(opt.ldscript),
    '/out': dirname(opt.out),
  };
  const argv = ['--gc-sections', '--warn-common', '--build-id=sha1',
                '--emit-relocs', '-Bstatic', '-EL', '--target2=rel',
                '-T', `/ld/${basename(opt.ldscript)}`];
  for (const o of objs) {
    mapdirs[`/o${argv.length}`] = dirname(o);
    argv.push(`/o${argv.length}/${basename(o)}`);
  }
  argv.push('-L/sdk/lib', '-lpebble');
  if (opt.libgcc) { mapdirs['/libgcc'] = dirname(opt.libgcc); argv.push(`/libgcc/${basename(opt.libgcc)}`); }
  if (opt.libc) { mapdirs['/libcdir'] = dirname(opt.libc); argv.push(`/libcdir/${basename(opt.libc)}`); }
  argv.push('-o', `/out/${basename(opt.out)}`);
  const status = runTool('lld.wasm', 'ld.lld', mapdirs, argv);
  failed = status !== 0;
}

rmSync(objdir, { recursive: true, force: true });
process.exit(failed ? 1 : 0);
