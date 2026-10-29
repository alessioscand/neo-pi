# neo-pi - how to build (version 0.6.1)

Build host: Linux x86_64.

1. **Toolchain**: xPack aarch64-none-elf-gcc 14.2.1-1.1 (newlib) in `toolchain/`
   (https://github.com/xpack-dev-tools/aarch64-none-elf-gcc-xpack/releases).
2. **Circle** (rsta2/circle) commit 7a81e1b, copied into `circle-rpi4/` and `circle-rpi3/`
   with this `Config.mk` (RASPPI = 4 or 3):

   ```
   AARCH = 64
   RASPPI = 4
   PREFIX64 = <path>/toolchain/xpack-aarch64-none-elf-gcc-14.2.1-1.1/bin/aarch64-none-elf-
   STDLIB_SUPPORT = 1
   DEFINE += -DKERNEL_MAX_SIZE=0x1000000
   ```

   KERNEL_MAX_SIZE is 16 MB because the core's Neo Geo CD buffers do not fit in the default 2 MB.
3. **Circle patch** (USB pads polled every 1 ms instead of the interval the device asks for,
   like `usbhid.jspoll=1` on Linux; hubs excluded):

       python3 patches/circle-usb-poll.py circle-rpi4
       python3 patches/circle-usb-poll.py circle-rpi3

   then `./makeall` in each copy, and `make` in `addon/SDCard` and `addon/fatfs`.
   `usbpoll=native` in `neopi/neopi.cfg` turns the faster polling off at run time.
4. **Geolith** in `third_party/geolith` (from the mirror github.com/alessioscand/geolith-mirror),
   unmodified; CD support is left out with `src/geo_disc_stub.c`.
5. `./build.sh all` builds `build/rpi4/kernel8-rpi4.img` and `build/rpi3/kernel8.img`.
6. **Raspberry Pi firmware**: raspberrypi/firmware commit 0641c5bf (the one Circle uses).

## Source files
- `src/kernel.cpp` - start-up, settings, game list, game loop, menus, benchmark, log
- `src/video.cpp` - double-buffered framebuffer, HVS/CPU integer scaling, scanlines, text in the black bands
- `src/ui.cpp` - menus and screens drawn at Neo Geo resolution
- `src/input.cpp` - USB pads and keyboard, control set-up (`neopi/pads.cfg`)
- `src/audio.cpp` - HDMI audio with drift correction
- `src/fontdata.c` - menu font ("Matinee", ZX Origins by Damien Guard)
- `src/vfs.cpp`, `src/ziphelp.c`, `src/shim.c` - file access, zip/PNG (miniz), minimal libc for the core
- `src/geo_disc_stub.c` - replaces the CD support
- `src/Makefile.in`, `build.sh`
- `test/harness.c` - Linux test harness (same core and set-up): frames as PPM, audio as WAV

## Testing without hardware
QEMU `-M raspi3b` with a Circle build using `-DNO_SDHOST -DNO_SCREEN_DMA_BURST_LENGTH`.
Adding `-DNEOPI_FAKEPAD` turns the USB keyboard into a fake pad (arrows = hat,
Q W E R T Y U I O P = buttons 1-10), since QEMU has no USB gamepad. Not in the real kernels.
