# neo-pi

A streamlined bare-metal Neo Geo emulator for Raspberry Pi.


## Design goals

neo-pi is built around a simple idea: nothing between you and the game.
No operating system, no desktop, nothing to configure: switch on and you
are in the game list, with a handful of clean menus, all driven from the
gamepad.

It respects the arcade spirit while keeping the conveniences that matter.
One save-state slot per game, for example: useful, without turning the game
into something else. Picture and controls get the same care: integer
scaling with square pixels, and pads read right before every frame.

It is also meant to give older boards a new lease on life: neo-pi makes the
most of the Raspberry Pi 3 and 4, so you don't need the latest hardware to
play.

Tested on the Raspberry Pi 4 and the Raspberry Pi 3 B+. On the Pi 3 most
games run at full speed with no overclocking. Scanlines are available on the Pi 4.


## Features

- Accurate emulation, built on the [Geolith](https://gitlab.com/jgemu/geolith)
  core
- Responsive controls: USB pads polled every 1 ms (1000 Hz), with no
  operating system in between
- Single-slot mode: with only one game on the card, neo-pi skips the list
  and boots straight into it, like an arcade board with one cartridge in
- MVS (arcade) and AES (home console) systems, all four BIOS regions
- Integer scaling with square pixels at 1080p or 720p, using the Pi's
  hardware scaler
- Optional scanlines (light, medium, strong) on the Pi 4, drawn within
  the same frame: no added lag
- Lazy finger (turbo): Select + B switches it on and off; while it is on,
  holding A, B, C or D fires repeatedly
- Save states (one slot per game), screenshots (PNG) and an optional
  status line (FPS, load, temperature, video mode)
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
3. Put `neogeo.zip` (MAME/FBNeo BIOS set) and your games in `.neo` format
   (made with TerraOnion's NeoBuilder) in the `neopi` folder.
4. Connect a pad and the TV (HDMI 0 on RPi4, next to the power socket) and switch on.


## Controls

The first time a pad model is connected, neo-pi asks you to press the
buttons you want as A, B, C, D, Start and Select. The d-pad, hat and left
stick always work. The second pad connected is Player 2.

| In game | |
|---|---|
| Select (short press) | Insert a coin (AES: Select) |
| Select, held 1 s | In-game menu: save / load state, reset, BIOS settings, game list, power off |
| Select + A | Screenshot (PNG) |
| Select + B | Lazy finger (turbo buttons) on/off |
| Select + C | Status line on/off |
| Select + D | Credits |

In the game list and in the menus, the buttons are shown on screen.
Always switch off with POWER OFF (in-game menu or options) before
unplugging: it saves memory cards and settings.


## Under the hood

A few choices behind neo-pi's low-latency design and performance optimizations:

- **One frame, start to finish, on one core.** Each frame reads the pads,
  emulates, queues the audio and draws, then the double-buffered
  framebuffer flips at the vertical sync. No pipeline: no frame is emulated
  ahead and shown later by another stage, so nothing adds a frame of delay.

- **1 ms USB polling.** The USB stack is patched so that pads are polled
  every 1 ms instead of the 8-10 ms most of them ask for, and they are read
  right before the frame is emulated.

- **The hardware scaler does the enlarging.** The 304x224 picture is copied
  once into a small framebuffer (480x270 at 1080p) and the Pi's HVS scales
  it 4x to the screen, integer and with square pixels: drawing costs about
  0.6 ms per frame even on a Pi 3. Scanlines are drawn while copying (CPU
  2x, HVS 2x), in the same frame.

- **Own audio resampler.** The YM2610 output (about 56 kHz) is brought to
  48 kHz by a small fixed-point resampler written for neo-pi; the
  general-purpose one used before took about 15% of the frame time on a
  Pi 3. The HDMI audio clock is not locked to the display, so the queue is
  kept at about two frames by stretching or shrinking blocks by a few
  samples: no drops, no clicks.

- **Measured, not guessed.** An empty `neopi/BENCH` file runs a built-in
  benchmark with a sampling profiler (the interrupted address is recorded
  every 200 us, `tools/resolve_profile.py` maps it to functions) and a check
  value of the final picture: an optimisation must give the same picture
  after the same 5,400 frames, so the emulation is unchanged. The
  emulation core is compiled with `-O3`, the rest with `-O2`.

- **Fixed clocks, no surprises.** The GPU core clock is fixed, the HDMI mode
  is forced to 1080p (or 720p), and the whole game is loaded into RAM before
  it starts: nothing is streamed from the SD card while you play.


## Files and troubleshooting

Everything neo-pi writes stays in the `neopi` folder: `saves/` (save states,
MVS memory, memory cards), `shots/` (screenshots), `neopi.cfg` (options),
`pads.cfg` (controls for each pad model), `LOG.TXT` (diagnostics).

For testing and performance comparisons, these lines can be added to
`neopi/neopi.cfg`:

| | |
|---|---|
| `filters=on` | allow scanlines on a Pi 3 too (experimental) |
| `usbpoll=native` | poll pads at the rate they ask for instead of every 1 ms |
| `resampler=speex` | previous, heavier audio resampler |
| `scaler=cpu` | scale with the CPU instead of the hardware scaler |

If something goes wrong, the green LED blinks a number of times, repeatedly:
2 = HDMI, 3 = SD card, 4 = BIOS, 5 = no `.neo` game, 7 = memory. With no
picture at all, put a `#` in front of `hdmi_group` and `hdmi_mode` in
`config.txt`. To report a problem, `neopi/LOG.TXT` is all that is needed;
for performance, add an empty `neopi/BENCH` file and send `BENCH.TXT` and
`PROFILE.TXT` too.

## Building from source

See [BUILD.md](BUILD.md). neo-pi is built with the xPack AArch64 GCC
toolchain on top of the Circle bare-metal library and the Geolith core.


## Credits

neo-pi is written by Alessio Scanderebech, alongside his coding buddy Claude, standing on the shoulders of:

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


## Licence

neo-pi is free software, released under the
[GNU General Public License v3](LICENSE) or any later version, as required
by Circle (GPLv3). The third-party components keep their own licences
(BSD 3-Clause and others); the full texts are in
[`dist/sd/LICENSES.txt`](dist/sd/LICENSES.txt).

NEO GEO is a trademark of SNK. neo-pi is an independent project, not
affiliated with or endorsed by SNK, and contains no BIOS, games or other
SNK material.