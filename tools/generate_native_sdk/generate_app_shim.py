# SPDX-FileCopyrightText: 2024 Google LLC
# SPDX-License-Identifier: Apache-2.0

import os.path
import subprocess
import tempfile

SHIM_PREAMBLE = """
.syntax unified
.thumb

"""

# We generate one of these trampolines for each function we export. This is a stub function
# that pretty much just sets us up to call 'jump_to_pbl_function', defined in the SHIM_FOOTER
# string.
SHIM_TEMPLATE = """
.section ".text.%(function_name)s"
.align 2
.thumb_func
.global %(function_name)s
.type %(function_name)s STT_FUNC
%(function_name)s:
        push {r0, r1, r2, r3}  @ Stack the original parameters. Remember that the caller
                               @ thinks they're calling a C function, not this shim function.
                               @ We don't want to touch these.
        ldr r1, =%(offset)d    @ Load up the index into the jump table array
        b jump_to_pbl_function @ See below...
 """

# This section defines the second part of the trampoline function. We only jump to it from
# the functions we define in SHIM_TEMPLATE. The SHIM_TEMPLATE function jumps to us after
# setting up the offset in the jump table in r1.
SHIM_FOOTER = """
.section ".text"
.type jump_to_pbl_function function
jump_to_pbl_function:
        adr r3, pbl_table_addr
        ldr r0, [r3]           @ r0 now holds the base address of the jump table
        add r0, r0, r1         @ add the offset to address the function pointer to the exported function
        ldr r2, [r0]           @ r2 now holds the address of the exported function
        mov r12, r2            @ r12 aka intra-procedure scratch register. We're allowed to
                               @ muck with this and not restore it
        pop {r0, r1, r2, r3}   @ Restore the original parameters to the exported C function
        bx r12                 @ And finally jump! Don't use branch and link, we want to
                               @ return to the original call site, not this shim

@ This pbl_table_addr variable will get compiled into our app binary. As part of our build process
@ the address of this variable will get poked into the PebbleProcessInfo meta data struct. Then, when
@ we load the app from flash we change the value of the variable to point the jump table exported
@ by the OS.
.align
pbl_table_addr:
.long 0xA8A8A8A8

"""


# riscv32: the same two parts. t0 and t1 are temporaries the caller does not
# expect kept, so the arguments in a0-a7 and on the stack go through untouched.
# The table address is data: code runs from the instruction-bus window of the
# RAM, which cannot be read, so it is in .data, which the code reaches at the
# data addresses.
RISCV32_SHIM_PREAMBLE = """
.option norelax
"""

RISCV32_SHIM_TEMPLATE = """
.section ".text.%(function_name)s","ax",@progbits
.align 1
.global %(function_name)s
.type %(function_name)s, @function
%(function_name)s:
        li t1, %(offset)d
        j jump_to_pbl_function
"""

RISCV32_SHIM_FOOTER = """
.section ".text.jump_to_pbl_function","ax",@progbits
.align 1
.type jump_to_pbl_function, @function
jump_to_pbl_function:
1:      auipc t0, %pcrel_hi(pbl_table_addr)
        lw t0, %pcrel_lo(1b)(t0)
        add t0, t0, t1
        lw t0, 0(t0)
        jr t0

.section ".data.pbl_table_addr","aw",@progbits
.align 2
.global pbl_table_addr
pbl_table_addr:
.word 0xA8A8A8A8
"""

SHIMS = {
    "armv7m": (SHIM_PREAMBLE, SHIM_TEMPLATE, SHIM_FOOTER),
    "riscv32": (RISCV32_SHIM_PREAMBLE, RISCV32_SHIM_TEMPLATE, RISCV32_SHIM_FOOTER),
}


def gen_shim_asm(functions, abi="armv7m"):
    preamble, template, footer = SHIMS[abi]
    output = []

    output.append(preamble)
    for idx, fun in enumerate(functions):
        if not fun.removed:
            output.append(template % {"function_name": fun.name, "offset": idx * 4})
    output.append(footer)

    return "\n".join(output)


ENTRY_SRC = os.path.join(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
    "sdk",
    "libpebble",
    "entry.c",
)
ENTRY_CFLAGS = [
    "-std=c11",
    "-Os",
    "-fPIE",
    "-ffunction-sections",
    "-fdata-sections",
    "-Wall",
    "-Werror",
]


class CompilationFailedError(Exception):
    pass


def _toolchain(abi):
    import sys

    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    from pebble_sdk_platform import ABI_TOOLCHAINS

    return ABI_TOOLCHAINS[abi]


def _compile(src, obj, flags, abi):
    toolchain = _toolchain(abi)
    gcc_process = subprocess.Popen(
        [
            toolchain["prefix"] + "gcc",
            *[f for f in toolchain["flags"] if f != "-fPIE"],
            *flags,
            "-c",
            "-o",
            obj,
            src,
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    output = gcc_process.communicate()
    if gcc_process.returncode != 0:
        print(output[1])
        raise CompilationFailedError()


def build_shim(shim_s, dest_dir, abi="armv7m"):
    shim_a = os.path.join(dest_dir, "libpebble.a")
    # Delete any existing archive, otherwise `ar` will append/insert to it:
    if os.path.exists(shim_a):
        os.remove(shim_a)

    # The shims are position independent by construction on riscv32, whose
    # apps are not PIC.
    entry_cflags = (
        ENTRY_CFLAGS if abi == "armv7m" else [f for f in ENTRY_CFLAGS if f != "-fPIE"]
    )
    with tempfile.TemporaryDirectory() as obj_dir:
        objs = [os.path.join(obj_dir, "pebble.o"), os.path.join(obj_dir, "entry.o")]
        _compile(shim_s, objs[0], ["-fPIC"] if abi == "armv7m" else [], abi)
        _compile(ENTRY_SRC, objs[1], entry_cflags, abi)

        ar_process = subprocess.Popen(
            # D: no timestamps or owners, the library is the same wherever
            # and whenever it is built.
            [_toolchain(abi)["prefix"] + "ar", "rcsD", shim_a, *objs],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        output = ar_process.communicate()
        if ar_process.returncode != 0:
            print(output[1])
            raise CompilationFailedError()


def make_app_shim_lib(functions, sdk_lib_dir, abi="armv7m"):
    temp_asm_fd, temp_asm_file = tempfile.mkstemp(suffix="pbl_shim.s")
    os.close(temp_asm_fd)
    with open(temp_asm_file, "w") as shim_s:
        shim_s.write(gen_shim_asm(functions, abi))

    build_shim(temp_asm_file, sdk_lib_dir, abi)
