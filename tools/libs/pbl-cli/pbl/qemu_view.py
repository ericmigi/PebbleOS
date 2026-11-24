# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

"""A window onto a headless Espressif QEMU: the display and the buttons.

Espressif's QEMU shows its RGB display in a window of at most 800x600, as the
controller scans the panel: a portrait panel mounted a quarter turn is shown
sideways. Its machines have no input device either. This window takes the
panel from the QEMU monitor's screendump instead, turns it upright and shows
it with whole pixels: at half its size by default, which on a panel scaled
twice is the firmware's own framebuffer, or at its size with --full. It only
redraws when the panel changed.

Its four buttons, and the keys of the native simulator (Up, Down; Right,
Return, space: Select; Left, BackSpace, Escape: Back), press the watch's
buttons through the console's `button` command, held for as long as they are
held past half a second. The command goes to the console port given, over a
connection of its own, or else to stdout, for the runner holding the console.

    python -m pbl.qemu_view MONITOR_SOCKET [--port PORT] [--full] [--title TITLE]
"""

import argparse
import os
import socket
import sys
import time

from .emulator import Monitor
from .errors import CommandContextError

# Back, Up, Select, Down as the console's button command numbers them
BUTTONS = (("Back", 0), ("Up", 1), ("Select", 2), ("Down", 3))
KEYS = {
    "Up": 1,
    "Down": 3,
    "Right": 2,
    "Return": 2,
    "space": 2,
    "Left": 0,
    "BackSpace": 0,
    "Escape": 0,
}
LONG_PRESS_MS = 500
POLL_MS = 50
POSITION = os.path.expanduser("~/.cache/pebbleos/qemu-view-position")


def upright(ppm, divisor):
    """A binary PPM turned upright if it is landscape, every divisor-th pixel
    of it, as a grey PGM; only the red channel is kept."""
    fields, end = [], 0
    while len(fields) < 4:
        start = end + len(ppm[end:]) - len(ppm[end:].lstrip())
        end = start + len(ppm[start:].split(maxsplit=1)[0])
        fields.append(ppm[start:end])
    magic, width, height, maxval = fields
    if magic != b"P6" or maxval != b"255":
        raise ValueError("not an 8-bit binary PPM")
    width, height = int(width), int(height)
    pixels = ppm[end + 1 :]
    stride = width * 3
    if width > height:
        # A quarter turn counter-clockwise: the right column becomes the top row
        rows = [pixels[x * 3 :: stride] for x in range(width - 1, -1, -1)]
    else:
        rows = [pixels[y * stride : (y + 1) * stride : 3] for y in range(height)]
    rows = [row[::divisor] for row in rows[::divisor]]
    header = b"P5 %d %d 255\n" % (len(rows[0]), len(rows))
    return header + b"".join(rows)


class View:
    def __init__(self, tk, monitor_path, port, divisor, title):
        self.tk = tk
        self.monitor_path = monitor_path
        self.port = port
        self.divisor = divisor
        self.dump = os.path.join(
            os.path.dirname(os.path.abspath(monitor_path)), "qemu-view.ppm"
        )
        self.monitor = None
        self.last = None
        self.pressed = {}

        self.root = tk.Tk()
        self.root.title(title)
        try:
            with open(POSITION) as f:
                self.root.geometry(f.read().strip())
        except OSError:
            pass
        self.screen = tk.Label(self.root, bg="black", bd=0)
        self.screen.pack()
        bar = tk.Frame(self.root)
        bar.pack(fill="x")
        for name, button in BUTTONS:
            widget = tk.Button(bar, text=name, width=1)
            widget.bind("<ButtonPress-1>", lambda e, b=button: self.down(b))
            widget.bind("<ButtonRelease-1>", lambda e, b=button: self.up(b))
            widget.pack(side="left", expand=True, fill="x")
        for key, button in KEYS.items():
            self.root.bind(f"<KeyPress-{key}>", lambda e, b=button: self.down(b))
            self.root.bind(f"<KeyRelease-{key}>", lambda e, b=button: self.up(b))
        self.status = tk.StringVar()
        tk.Label(self.root, textvariable=self.status, anchor="w").pack(fill="x")
        self.root.protocol("WM_DELETE_WINDOW", self.close)

    def down(self, button):
        self.pressed.setdefault(button, time.monotonic())

    def up(self, button):
        start = self.pressed.pop(button, None)
        if start is None:
            return
        held = int((time.monotonic() - start) * 1000)
        if held > LONG_PRESS_MS:
            self.command(f"button hold {button} {held}")
        else:
            self.command(f"button click {button}")

    def command(self, line):
        if self.port is None:
            print(line, flush=True)
            self.status.set(line)
            return
        try:
            with socket.create_connection(("127.0.0.1", self.port), timeout=0.5) as s:
                s.sendall(line.encode() + b"\r")
                s.shutdown(socket.SHUT_WR)
            self.status.set(line)
        except OSError as e:
            self.status.set(f"console port {self.port}: {e.strerror or e}")

    def refresh(self):
        try:
            if self.monitor is None:
                self.monitor = Monitor(self.monitor_path).__enter__()
            self.monitor.command(f"screendump {self.dump}")
            with open(self.dump, "rb") as f:
                ppm = f.read()
            if ppm != self.last:
                self.last = ppm
                image = self.tk.PhotoImage(
                    data=upright(ppm, self.divisor), format="ppm"
                )
                self.screen.configure(image=image)
                self.screen.image = image
            delay = POLL_MS
        except (OSError, ValueError, CommandContextError, self.tk.TclError):
            # QEMU not up yet, restarting, or gone
            if self.monitor is not None:
                self.monitor.__exit__()
            self.monitor = None
            delay = 1000
        self.root.after(delay, self.refresh)

    def save_position(self):
        os.makedirs(os.path.dirname(POSITION), exist_ok=True)
        with open(POSITION, "w") as f:
            f.write(f"+{self.root.winfo_x()}+{self.root.winfo_y()}")

    def keep_position(self):
        # The runner ends the window with QEMU, without a close to save it on
        self.save_position()
        self.root.after(5000, self.keep_position)

    def close(self):
        self.save_position()
        self.root.destroy()

    def run(self):
        self.refresh()
        self.root.after(5000, self.keep_position)
        self.root.mainloop()


def main():
    parser = argparse.ArgumentParser(
        prog="pbl.qemu_view", description=__doc__.split("\n")[0]
    )
    parser.add_argument("monitor", help="QEMU monitor socket")
    parser.add_argument("--port", type=int, help="console TCP port (default: stdout)")
    parser.add_argument("--full", action="store_true", help="the panel at its own size")
    parser.add_argument("--title", default="PebbleOS on QEMU", help="window title")
    args = parser.parse_args()
    # A standalone Python finds its Tcl and Tk only when told, from a venv
    for lib in ("tcl8.6", "tk8.6"):
        path = os.path.join(sys.base_prefix, "lib", lib)
        if os.path.isdir(path):
            os.environ.setdefault(lib[:3].upper() + "_LIBRARY", path)
    import tkinter

    View(tkinter, args.monitor, args.port, 1 if args.full else 2, args.title).run()


if __name__ == "__main__":
    main()
