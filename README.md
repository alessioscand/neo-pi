# neo-pi

A streamlined bare-metal Neo Geo emulator for the Raspberry Pi.

It does one thing and keeps everything else out of the way: low input lag
and a steady 60 frames per second. No operating system, no desktop, no
setup wizard, nothing to configure. Switch on and you are in the game list,
with a handful of clean menus, all driven from the gamepad.

Tested on the Raspberry Pi 4 and the Raspberry Pi 3 B+. On the Pi 3 most
games run at full speed; the heaviest, such as Metal Slug 3, can still slow
down for a moment in the busiest scenes.

## Features

- Accurate emulation, built on the [Geolith](https://gitlab.com/jgemu/geolith)
  core
- Responsive controls: USB pads are polled every 1 ms (1000 Hz) instead
  of the usual 8-10 ms, and are read right before each frame is emulated,
  with no operating system in between
- Single-slot mode: with only one game on the card, neo-pi skips the list
  and boots straight into it, like an arcade board with one cartridge in
- MVS (arcade) and AES (home console) systems, all four BIOS regions
- Integer scaling with square pixels at 1080p or 720p, using the Pi's
  hardware scaler
- Optional scanlines (light, medium, strong) on the Pi 4, drawn within
  the same frame: no added lag
- Lazy finger (turbo): Select + B switches it on and off; while it is on,
  holding A, B, C or D fires repeatedly
- Save states, screenshots (PNG) and an optional status line (FPS, load,
  temperature, video mode)
- Safe power off that saves memory cards, MVS settings and the log

Planned:

- Composite video output for CRT TVs, through the Pi's 3.5 mm AV jack
- UniBIOS support
- Further Raspberry Pi 3 optimizations

## Requirements

- Raspberry Pi 4 (the Pi 400 should work too) or Raspberry Pi 3 B+
- A micro SD card formatted FAT32
- A USB gamepad and an HDMI TV or monitor
- Your own Neo Geo BIOS (`neogeo.zip`) and games in `.neo` format.
  **They are not included and are never distributed with neo-pi.**

## Getting started

1. Download the SD card package from the
   [Releases](../../releases) page.
2. Copy everything in it to the root of the card.
3. Put `neogeo.zip` and your `.neo` games in the `neopi` folder.
4. Connect a pad and the TV (HDMI 0, next to the power socket) and switch on.

The full guide (controls, menus, options, files) is `README.txt` in the
SD card package.

## Building from source

See [BUILD.md](BUILD.md). neo-pi is built with the xPack AArch64 GCC
toolchain on top of the Circle bare-metal library and the Geolith core.

## Credits

neo-pi is written by **Alessio Scanderebech**, standing on the shoulders of:

| | |
|---|---|
| Neo Geo core | [Geolith](https://gitlab.com/jgemu/geolith) by Rupert Carmichael |
| 68000 CPU | Musashi by Karl Stenerud |
| Z80 CPU | by Nicola Allemand and Rupert Carmichael |
| Sound | ymfm by Aaron Giles |
| Resampler (optional) | speexdsp by Jean-Marc Valin |
| Zip and PNG | miniz by Rich Geldreich |
| Bare-metal system | [Circle](https://github.com/rsta2/circle) by Rene Stange |
| Menu font | "Matinee" from [ZX Origins](https://damieng.com/typography/zx-origins/) by Damien Guard |

## Thanks

To Razoola, Andrea Mazzoleni and Rubén Tomás Alonso, for showing the way.

## Licence

neo-pi is free software, released under the
[GNU General Public License v3](LICENSE) or any later version, as required
by Circle (GPLv3). The third-party components keep their own licences
(BSD 3-Clause and others); the full texts are in
[`dist/sd/LICENSES.txt`](dist/sd/LICENSES.txt).

NEO GEO is a trademark of SNK. neo-pi is an independent project, not
affiliated with or endorsed by SNK, and contains no BIOS, games or other
SNK material.
