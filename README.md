# neo-pi

A streamlined, bare-metal Neo Geo emulator for Raspberry Pi 3 & 4, with no unnecessary layers and highly responsive input.

Website: [alessioscand.github.io/neo-pi](https://alessioscand.github.io/neo-pi/)

If you enjoy neo-pi or [nes-pi](https://alessioscand.github.io/nes-pi/)
please consider supporting me:
[paypal.me/alescandy](https://paypal.me/alescandy)

## Why neo-pi

I discovered the Neo Geo in the arcades as a kid, and I've been fascinated
by it ever since. I started collecting Neo Geo CD games in the early 2000s,
briefly moved to AES, and eventually settled on MVS. I still own an MV1-FZ
board and several cartridges, but rarely get to enjoy them due to the space
and setup they require.

Ever since the days of Raspberry Pi 2 with AdvanceMAME and VGA666 hat, I've
dreamed of creating a focused, compact Neo Geo experience on the Raspberry
Pi, as close to the original hardware as possible, but with all the
conveniences of modern emulation (LCD displays with visual filters, save
states, turbo buttons) but prioritizing the responsiveness of the controls.
I've tried countless times to strip existing retro-gaming distros down to
their bare essentials, and thanks to today's vibe-coding tools, I've finally
been able to bring my original vision to life. Still, neo-pi isn't just
code: it's design, choices and compromises, and those still take time and
care. So let's look at how it's made, the old-fashioned way.

## The choices behind it

**Bare metal.** Nothing sits between the game and the screen: no operating
system on the Pi's CPU and no graphics stack adding latency. neo-pi IS the
kernel itself: it runs directly on the Pi's hardware, boots in seconds and
talks to the hardware directly, down to the video scaler's registers.
**Circle** provides the essential drivers (USB, SD, HDMI, audio), so bare metal
doesn't mean starting from scratch.

**Geolith.** This accurate Neo Geo emulator is a real gem, written in plain,
portable C that doesn't assume an OS: it renders into a buffer it is given,
talks to the outside world through a few callbacks, and loads the BIOS and
games straight from memory. That made it a natural fit for Circle.

**Scanline-based rendering.** Above all, Geolith draws the picture the way
the real Neo Geo does: one line at a time, as the emulated video chip
reaches it. Each line is drawn always at the same moment and never touched
again, so it can be handed over as soon as it's done. This opened up a
whole range of possibilities. The lines can be drawn on a second CPU core
while the first runs the game. And since each line is ready while the frame
is still being emulated, it can be sent to the screen just before the HDMI
output reaches it: *beam racing!* So the pads are read only a couple of
milliseconds before the picture appears, as they would be on the original
hardware with a CRT.

**Custom audio resampler.** The Neo Geo's sound chip runs at about 56 kHz,
while HDMI wants 48 kHz. Instead of a heavy general-purpose converter,
neo-pi uses a small one written just for it: same clean sound, a fraction
of the work (on a Pi 3, Geolith's standard Speex resampler took about 15%
of every frame).

**User experience.** The rest was a matter of choices. Display options are
cut to the bone: integer scaling is always on, and the only filters are
scanlines and an NTSC composite look, so you just play, with nothing to
tweak and nothing to distract you. Save states are limited to one slot per
game: enough to soften the gameplay (think of the notoriously brutal bosses
of some King of Fighters games) while keeping that edge-of-your-seat tension
that only arcade games can give you.

## Features

**Gameplay**

- Accurate emulation, built on the [Geolith](https://gitlab.com/jgemu/geolith)
  core: MVS (arcade), AES (home console) and the Universe BIOS, all four
  BIOS regions
- Responsive controls: pads are read every millisecond, and what you press
  shows up on screen almost as quickly as on the original hardware with a
  CRT
- Beam racing: each line is sent to the screen just after it is drawn, so
  the pads are read a couple of milliseconds before the picture appears
- *Lazy finger* (turbo buttons): Select + B toggles it; while on, holding A, B, C or D
  fires repeatedly
- Save states: one slot per game

**Picture**

- Integer scaling with square pixels at 1080p or 720p, done by the Pi's
  hardware scaler
- Optional scanlines (light, medium, strong), drawn within the same frame:
  no added lag
- Blargg's NTSC filter, the picture of a composite cable on a TV
  of the time, filtered line by line on a core of its own: no added lag

**Convenience**

- MV-1 mode: with only one game on the SD card, neo-pi skips the
  list and boots straight into it, like an arcade board
- Screenshots (PNG) and an optional status line (FPS, load, input lag,
  temperature, video mode)
- Safe power off that saves memory cards, MVS settings and the log

**Planned:**

- Composite video output for CRT TVs, through the Pi's 3.5 mm AV jack


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

Although wireless gamepads are supported, wired ones are preferable for maximum
responsiveness. The first time a pad model is connected, neo-pi asks you to press
the buttons you want as A, B, C, D, Start and Select. The d-pad, hat and left
stick always work. Second pad connected is Player 2.

| In game | |
|---|---|
| Select (short press) | Insert a coin (AES: Select) |
| Select, held 1 s | In-game menu: save / load state, BIOS settings, options, reset, game list, power off |
| Select + A | Screenshot (PNG) |
| Select + B | *Lazy finger* (turbo buttons) on/off |
| Select + C | Status line on/off |
| Select + D | Credits |

**With the Universe BIOS** (OPTIONS > SYSTEM > UNIBIOS, needs
`uni-bios_4_0.rom` in `neogeo.zip`), neo-pi's own controls stay the same,
so a few of its combinations take a different route:

| Universe BIOS | On neo-pi |
|---|---|
| Main menu (A+B+C at start-up) | In-game menu > BIOS SETTINGS, or A+B+C while its splash screen is shown |
| In-game menu (Start+A+B+C) | Works as is |
| In-game menu (Start+Coin, Start+Select) | Not available: Select belongs to neo-pi (coin, menu, shortcuts); use Start+A+B+C |
| Memory card manager (A+B+C+D at start-up), test mode (B+C+D) | Press them while its splash screen is shown |

The region is set in the Universe BIOS menu, and its settings are kept
for each game, separate from the MVS BIOS ones: switching SYSTEM back and
forth never mixes them.

In the menus the buttons are shown on screen.
Always switch off with POWER OFF (in-game menu or options) before
unplugging: it saves memory cards and settings.


## Tinkerer's corner

Everything neo-pi writes stays in the `neopi` folder: `saves/` (save states,
MVS memory, Universe BIOS settings, memory cards), `shots/` (screenshots), `neopi.cfg` (options),
`pads.cfg` (controls for each pad model), `LOG.TXT` (diagnostics).

**Reading the status line.** Select + C shows FPS, LOAD, temperature,
RACE (or DLY) and the video mode.

- **LOAD** is how much of a frame the busiest moment of the last second
  needed: below 100% no frame is lost.
- **RACE** is the time, in ms, from reading the pads to the top of the
  picture leaving the Pi: this is neo-pi's share of the lag, the only part
  that depends on the emulator. On an original Neo Geo the same step takes
  about 2 ms, so a RACE of 2-4 ms means neo-pi adds next to nothing. The
  pad itself, the TV (use its game mode) and the game's own reaction time
  come on top, as they would with any system.
- **DLY** replaces RACE when *beam racing* is off: it is the frame delay,
  how long after the vsync the pads are read.

**Testing options.** The defaults are what neo-pi is designed and tuned
for: the lines below are only meant for testing and comparisons, not for
everyday play. Add them to `neopi/neopi.cfg`, one per line:

| Option | |
|---|---|
| `beamracing=off` | double buffer instead of beam racing (more lag) |
| `framedelay=off` | no automatic frame delay |
| `framedelay=5` | fixed frame delay in ms (1-12; too much = stutter) |
| `multicore=off` | everything on one core, as up to 0.6.3 (heavier; on a Pi 3, no scanlines or COMPOSITE) |
| `usbpoll=500` | poll pads every 2 ms instead of every 1 ms (if a pad misbehaves at 1000 Hz) |
| `usbpoll=250` | poll pads every 4 ms |
| `usbpoll=native` | poll pads at the rate they ask for instead of every 1 ms |
| `resampler=speex` | Geolith's standard Speex resampler (heavier) |
| `scaler=cpu` | scale with the CPU instead of the hardware scaler (slower) |

An empty `neopi/BENCH` file runs a built-in benchmark on the selected game
(results in `BENCH.TXT` and `PROFILE.TXT`); an empty `neopi/MCTEST` file
checks that one core and two cores give the same picture, line by line.

**If something goes wrong.** The green LED blinks a number of times,
repeatedly: 2 = HDMI, 3 = SD card, 4 = BIOS, 5 = no `.neo` game,
7 = memory. With no picture at all, put a `#` in front of `hdmi_group` and
`hdmi_mode` in `config.txt`. To report a problem, `neopi/LOG.TXT` is all
that is needed; for performance, add an empty `neopi/BENCH` file and send
`BENCH.TXT` and `PROFILE.TXT` too.

**Heat.** If games start to slow down after a while, it is usually heat: a
heatsink is recommended. The status line (Select + C) shows the
temperature; the Pi 3 B+ lowers its CPU clock above 60 °C, the Pi 4 from
about 80 °C.

## Building from source

See [BUILD.md](BUILD.md). neo-pi is built with the xPack AArch64 GCC
toolchain on top of the Circle bare-metal library and the Geolith core.


## Credits

neo-pi is written by Alessio Scanderebech and his much-maligned programming
partner Claude, standing on the shoulders of giants:

| Component | Thanks to |
|---|---|
| Bare-metal system | [Circle](https://github.com/rsta2/circle) by Rene Stange |
| Neo Geo core | [Geolith](https://gitlab.com/jgemu/geolith) by Rupert Carmichael |
| 68000 CPU | Musashi by Karl Stenerud |
| Z80 CPU | by Nicolas Allemand and Rupert Carmichael |
| Sound | ymfm by Aaron Giles (C port by Rupert Carmichael) |
| Resampler (optional) | speexdsp by Jean-Marc Valin |
| Zip and PNG | miniz by Rich Geldreich |
| Menu font | "Matinee" from [ZX Origins](https://damieng.com/typography/zx-origins/) by Damien Guard |
| NTSC filter | by Shay Green (blargg) |


## License

neo-pi is free software, released under the
[GNU General Public License v3](LICENSE) or any later version, as required
by Circle (GPLv3). The third-party components keep their own licences
(BSD 3-Clause, LGPL and others); the full texts are in
[`dist/sd/LICENSES.txt`](dist/sd/LICENSES.txt).

NEO GEO is a trademark of SNK. neo-pi is an independent project, not
affiliated with or endorsed by SNK, and contains no BIOS, games or other
SNK material.
