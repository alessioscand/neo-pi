# neo-pi - how to build

Build host: Linux x86_64.

1. **Toolchain**: xPack aarch64-none-elf-gcc 14.2.1-1.1 (newlib) in `toolchain/`
   (https://github.com/xpack-dev-tools/aarch64-none-elf-gcc-xpack/releases).
2. **Circle** (rsta2/circle) commit 7a81e1b, copied into `circle-rpi4mc/` and `circle-rpi3mc/`
   with this `Config.mk` (RASPPI = 4 or 3; the circle-rpiN directory names inside it adjusted):

   ```
   AARCH = 64
   RASPPI = 4
   PREFIX64 = <path>/toolchain/xpack-aarch64-none-elf-gcc-14.2.1-1.1/bin/aarch64-none-elf-
   STDLIB_SUPPORT = 1
   DEFINE += -DKERNEL_MAX_SIZE=0x1000000
   DEFINE += -DARM_ALLOW_MULTI_CORE
   ```

   ARM_ALLOW_MULTI_CORE: the video chip's lines are drawn on core 1 (see step 4),
   core 2 runs the COMPOSITE filter.

   KERNEL_MAX_SIZE is 16 MB because the core's Neo Geo CD buffers do not fit in the default 2 MB.
3. **Circle patches**: USB pads polled every 1 ms instead of the interval the device asks for,
   like `usbhid.jspoll=1` on Linux (hubs excluded); third-party wired Xbox 360 (XInput)
   pads handled by Circle's Xbox 360 driver, like Linux xpad:

       python3 patches/circle-usb-poll.py circle-rpi4mc
       python3 patches/circle-usb-poll.py circle-rpi3mc
       python3 patches/circle-xinput.py circle-rpi4mc
       python3 patches/circle-xinput.py circle-rpi3mc

   then `./makeall` in each copy, and `make` in `addon/SDCard` and `addon/fatfs`.
   `usbpoll=500` / `usbpoll=250` in `neopi/neopi.cfg` poll every 2 / 4 ms instead, `usbpoll=native`
   turns the faster polling off at run time.
4. **Geolith** in `third_party/geolith` (from the mirror github.com/alessioscand/geolith-mirror),
   unmodified; CD support is left out with `src/geo_disc_stub.c`. `build.sh` applies
   `patches/geolith-mc.py` to the copy of `geo_lspc.c` in the build directory: core 0
   queues the lines, core 1 draws them (and copies each finished line to the screen);
   before anything the drawing reads is changed, core 0 waits for core 1, so the
   picture is identical to single core. `multicore=off` in `neopi/neopi.cfg` keeps
   everything on core 0; an empty `neopi/MCTEST` file compares single core and
   multicore line by line (results in `neopi/MCTEST.TXT`).
   It then applies `patches/geolith-phase.py` to the copies of `geo.c` and `geo_lspc.c`:
   each emulated frame starts at line 248, so the vertical blank (when games read the
   pads) comes first and the picture drawn after it is shown at the next vsync.

   **snes_ntsc** 0.2.2 by Shay Green (blargg), LGPL 2.1+, in `third_party/snes_ntsc`,
   unmodified: the COMPOSITE filter. `src/composite.c` includes it with its own
   configuration; `src/tvcore.cpp` runs it on core 2, one line at a time as core 1
   finishes it.
5. `./build.sh all` builds `build/rpi4/kernel8-rpi4.img` and `build/rpi3/kernel8.img`.
   The Geolith core is compiled with `-O3` (the rest of neo-pi and Circle with `-O2`):
   on a Pi 3 it halves the frames that miss 60 Hz in heavy scenes, with identical
   emulation. `GEO_OPTIMIZE=-O2 ./build.sh 3` builds the core the old way, for comparison.
6. **Raspberry Pi firmware**: raspberrypi/firmware commit 0641c5bf (the one Circle uses),
   copied into `dist/sd/` (bootcode.bin, start*.elf, fixup*.dat, bcm2711-*.dtb,
   armstub8-rpi4.bin, LICENCE.broadcom).
7. `./make_release.sh` checks and packages `dist/neo-pi-<version>-sd.zip` (see below).

## Source files
`src/` is neo-pi's own; `common/` has the files that are the same in m92-pi
(nothing Neo Geo in them: what they need comes from `src/project.h`).

- `src/project.h` - name, version, picture size, buttons, LED error codes
- `src/kernel.h`, `src/kernel.cpp` - the kernel class; start-up, starting cores 1 and 2, the main flow, power off
- `src/console.cpp` - the start-up log on screen, serial and `LOG.TXT`; fatal errors
- `src/settings.cpp` - `neopi.cfg`, the video mode it asks for, `pads.cfg`
- `src/games.cpp` - the list of .neo files, loading a game with the BIOS, save data
- `src/menus.cpp` - game list, OPTIONS, CONTROLS, power off countdown, credits
- `src/gameloop.cpp` - pads to the Neo Geo panels, one emulated frame per screen frame, the in-game menu, the status line
- `src/frame.cpp` - the emulated frame: picture and sound buffers, where the lines go, audio resampling
  (linear, 56.3 kHz to 48 kHz; `resampler=speex` uses Geolith's Speex path)
- `src/racing.cpp` - beam racing state for the game loop
- `src/report.cpp`, `src/bench.cpp` - the per-minute report in the log; BENCH and MCTEST
- `src/mcline.cpp` - the multicore line drawing of `patches/geolith-mc.py`, as the kernel sees it
- `src/screen.h` - `CScreen`, the only interface the kernel draws through; `src/video.cpp` is
  the HDMI one (double-buffered framebuffer, HVS/CPU integer scaling, scanlines, text in the black bands).
  A CRT output on the composite jack would be another `CScreen`, chosen in `kernel.h`
- `src/ui.cpp` - menus and screens drawn at Neo Geo resolution; `src/fontdata.c` - menu font ("Matinee", ZX Origins by Damien Guard)
- `src/vfs.cpp`, `src/ziphelp.c`, `src/shim.c` - file access, zip/PNG (miniz), minimal libc for the core
- `src/geo_disc_stub.c` - replaces the CD support
- `common/smp.h` - work shared between cores: `SharedLoad` / `SharedStore`, `SharedAcquire` / `SharedRelease`, events, `Ticks`
- `common/raster.cpp`, `common/race.cpp` - beam racing: where the HDMI output is (HVS), and when each line can be sent
- `common/composite.c`, `common/tvcore.cpp` - COMPOSITE filter (snes_ntsc) and its core 2 queue
- `common/input.cpp` - USB pads and keyboard, control set-up (`neopi/pads.cfg`)
- `common/audio.cpp` - HDMI audio with drift correction
- `common/profile.cpp` - sampling profiler used by the benchmark (`neopi/PROFILE.TXT`)
- `common/fbmap.cpp` - maps the framebuffer as write-combining memory
- `common/pacing.cpp` - frame phase and frame delay (without beam racing)
- `common/stats.cpp` - timing statistics: histograms, averages, percentiles
- `src/Makefile.in`, `build.sh`, `make_release.sh`
- `tools/resolve_profile.py` - turns `PROFILE.TXT` into time per function, using the kernel's .elf
- `test/harness.c` - Linux test harness (same core and set-up): frames as PPM, audio as WAV
- `test/race_sim.cpp` - host simulation of the beam racing plan (late or overwritten lines)

Everything, Geolith included, is built with `-Wall -Wextra -Wshadow` and must
stay free of warnings.

## Testing without hardware
`./build.sh qemu` builds `build/rpiqemu/kernel8.img` for QEMU `-M raspi3b`, with a
Circle copy in `circle-qemumc/` built with `-DNO_SDHOST -DNO_SCREEN_DMA_BURST_LENGTH`.
`PROJECT_FAKEPAD` turns the USB keyboard into a fake pad (arrows = hat,
Q W E R T Y U I O P = buttons 1-10), since QEMU has no USB gamepad. Not in the real kernels.

## Benchmark and profile
An empty file `neopi/BENCH` (no extension) on the card runs a benchmark on the
selected game before the list: 30 s of the game's demo, then 60 s measured at
full speed. `neopi/BENCH.TXT` has the frame times, the slowest frames and a
check value of the final picture (the same build options must give the same
value); `neopi/PROFILE.TXT` has where the time went, sampled every 200 us.
To read it, keep the `.elf` of the kernel that produced it:

    python3 tools/resolve_profile.py build/rpi3/kernel8.elf PROFILE.TXT

## Release
`./make_release.sh` (after `./build.sh all`) checks that both kernels are built
and carry the version in `src/project.h`, that the firmware files are in `dist/sd/`
and that no BIOS, games, BENCH file or other user data are in the package, then
writes `dist/neo-pi-<version>-sd.zip`. The source archive comes from the GitHub
release itself (tag `v<version>`).
