# Pebble PBW loader and syscall probe

This standalone Zephyr app validates the loader contract with the real Sliding
Text emery binary and exercises one Pebble-shaped syscall through the custom
Zephyr svc #4 hook. It does not start the PBW entry point.

## Loader result

The checked-in blob is the 3228-byte Sliding Text
emery/pebble-app.bin. Its packed PebbleProcessInfo is:

- header version 16.0; SDK version 5.95
- load_size = 3020, virtual_size = 3024
- entry offset 0x5b8
- SDK jump-table pointer slot 0xb8
- flags 0x141 (watchface plus emery)
- 52 relocation entries
- stored CRC 0x9a4755c9

The app compiles PebbleOS's real src/fw/util/legacy_checksum.c. It validates
the header and CRC before copying, copies the load image and temporary
relocation table into an aligned RAM segment, applies the same bytewise
relocation algorithm as process_loader_storage.c, clears the relocation table
back to zero for .bss, and patches the SDK table slot last.

Expected UART:

    LOADER: header ok (crc=0x9a4755c9 version=16.0 entry=0x000005b8)
    LOADER: relocated 52 entries
    LOADER: jumptable patched
    LOADER_OK
    SYSCALL: DEFINE_SYSCALL svc#4 returned 42
    SYSCALL_OK

Build from the Zephyr workspace:

    export ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb
    export GNUARMEMB_TOOLCHAIN_PATH="$(dirname "$(dirname "$(command -v arm-none-eabi-gcc)")")"
    .venv/bin/west build -b pt2 \
      /Users/eric/dev/pebbleos-zephyr/zephyr-port-apps/loader \
      -d /Users/eric/dev/pebbleos-zephyr/build-loader

In a restricted Codex sandbox, add
-- -DUSER_CACHE_DIR=/Users/eric/dev/pebbleos-zephyr/build-loader/.cache
to keep Zephyr's compiler-capability cache in a writable directory.

## Real SDK table path

There are two distinct dispatches; they should not be collapsed into one.

1. The app-side SDK shim dispatches by stable table index. The generated
   trampoline loads the pointer patched into the PBW's slot at 0xb8, adds
   index * 4, loads a firmware function pointer, and tail-branches to it.
2. A DEFINE_SYSCALL wrapper raises privilege. It does not encode a
   per-function syscall number. After SVC return, its static
   branch to __function_name selects the privileged implementation.

For a concrete time call, the current generated table has pbl_override_time
at index 519 (byte offset 2076). The PBW's time() shim tail-calls that entry.
pbl_override_time() calls sys_get_time(); sys_get_time is a DEFINE_SYSCALL,
and __sys_get_time() calls rtc_get_time(). Existing append-only SDK ordering
must be preserved because the target app was built against SDK 5.95. Other
useful reference entries are tick_timer_service_subscribe at index 262 and
graphics_draw_text at index 309.

The full Zephyr firmware must build the existing tools/generate_native_sdk
output and export the real g_pbl_system_tbl. The placeholder in this probe
only proves the loader's pointer patch.

## svc #4 integration

The Zephyr fork's pebble-sandbox-spike branch already reserves SVC immediate 4
and calls z_arm_custom_svc_hook(exception_frame, EXC_RETURN, 4) before Zephyr
handles its own SVC values. Integrate the real Pebble syscall path as follows:

1. Change svc 2 to svc 4 in DEFINE_SYSCALL in
   src/fw/syscall/syscall_internal.h. The SVC instruction remains two bytes,
   so syscall_internal_maybe_skip_privilege() still skips it with
   addeq lr, #2.
2. Change the private re-entry SVC in mcu_call_unprivileged() in
   src/fw/syscall/syscall_internal.c from 2 to 4. Keep its separate exact-PC
   and active-call-state checks; it is intentionally outside .syscall_text.
3. Preserve the Pebble linker island: __syscall_text_start__, all
   .syscall_text.* wrappers, then __syscall_text_end__. The demo uses an exact
   callsite check because it has one wrapper. Production must accept SVC 4
   only when the stacked return PC is inside that island, or when the
   separately authorized mcu_call_unprivileged re-entry predicate succeeds.
4. In the strong Zephyr z_arm_custom_svc_hook, reject unless the current
   thread is S1's App or Worker, the exception came from unprivileged Thread
   mode using PSP, the exception frame and pre-SVC SP are inside that
   process's stack, MSP is outside it, and the stacked PC passes the gate
   above. An invalid SVC must fault/terminate that process, not return as if it
   succeeded.
5. Reproduce the old CM33 port setup before raising privilege. Use EXC_RETURN
   plus xPSR bit 9 to calculate the pre-SVC SP for basic or floating-point
   frames. Save the original stacked LR and pre-SVC SP in per-thread process
   state. Rewrite the stacked LR to prv_drop_privilege. If dedicated
   privileged syscall stacks are retained, copy the exception frame and
   enough caller stack-argument words, then update PSP and PSPLIM while still
   in Handler mode.
6. Clear CONTROL.nPRIV and exception-return. Registers r0-r3 still contain the
   app arguments. The stacked PC resumes after svc #4, executes the wrapper's
   branch to __function_name, and the real body runs privileged.
7. The body returns through the rewritten LR. prv_drop_privilege preserves
   r0-r1, performs syscall-exit bookkeeping, restores the app PSP/PSPLIM if a
   privileged syscall stack was used, sets CONTROL.nPRIV, executes an ISB, and
   branches to the saved app return address. This carries scalar and 64-bit
   return values back unchanged.

PRIVILEGE_WAS_ELEVATED depends on the privileged body's return address being
prv_drop_privilege. Keep that invariant so pointer-taking calls such as
sys_get_time_ms() validate every app buffer with
syscall_assert_userspace_buffer(). Validation must use S1's current app RAM
and stack bounds, exclude privileged syscall-stack memory, handle zero length
explicitly, and reject arithmetic overflow.

The demo in src/syscall_demo.c compiles and links this flow with one local
DEFINE_SYSCALL: an unprivileged Zephyr thread calls syscall_demo_double(21),
the svc#4 hook raises privilege, the implementation returns 42, the drop
trampoline restores unprivileged mode, and the app passes 42 through the same
syscall so privileged code can confirm the return reached the caller.

## Dependencies on S1

S1 must provide the process/thread context that replaces the current FreeRTOS
task and TLS assumptions:

- stable App/Worker k_thread identity and current-process lookup
- loaded image, app stack, and optional privileged syscall-stack bounds
- per-thread saved syscall LR/SP and nested syscall/callback state
- MPU restore programming on every incoming thread switch
- process-fault/exit handling for rejected SVCs and bad user pointers
- current process metadata and app-state ownership used by applib functions

For the first real PBW run, its RAM image must be user executable and writable
(the legacy image has no text/data split), its stack user RW/XN, firmware flash
containing app-callable applib code user RO/executable, and kernel RAM
privileged-only. g_pbl_system_tbl must be user-readable and read-only. The SVC
return-PC island is the privilege boundary: direct calls to privileged bodies
remain unprivileged and must not gain kernel access.

After loading, perform any required D-cache clean and I-cache invalidate before
S1 starts (segment + info.offset) | 1 as an unprivileged thread. S1 also owns
calling that entry point and process teardown; this probe deliberately stops
after relocation and table patching.
