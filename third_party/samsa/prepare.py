# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

"""Prepare Samsa's firmware sources and patched regression tests in the build tree."""

import filecmp
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def prepare(stock, output, patches):
    output.mkdir(parents=True, exist_ok=True)
    inputs = [
        *stock.glob("src/*.[ch]"),
        *stock.glob("include/samsa/*.h"),
        stock / "tests/test_control.c",
    ]
    with tempfile.TemporaryDirectory(dir=output.parent) as temp:
        tree = Path(temp)
        for source in inputs:
            target = tree / source.relative_to(stock)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)

        # Apply as plain files even when the build directory is inside a Git checkout.
        env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
        env["GIT_CEILING_DIRECTORIES"] = str(tree.parent)
        for patch in patches:
            subprocess.run(["git", "apply", str(patch)], cwd=tree, env=env, check=True)

        for source in inputs:
            relative = source.relative_to(stock)
            built, target = tree / relative, output / relative
            if target.is_file() and filecmp.cmp(built, target, shallow=False):
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            built.replace(target)


if __name__ == "__main__":
    prepare(
        *(Path(v).resolve() for v in sys.argv[1:3]),
        [Path(v).resolve() for v in sys.argv[3:]],
    )
