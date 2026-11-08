neo-pi 1.0.0 - Bare-metal NEOGEO emulator for Raspberry Pi 3 and 4
============================================================================

WHAT'S NEW IN 1.0.0

  - The first stable release: the code has been reorganised from top to
    bottom, also to provide future support for CRT monitors.
  - To update a card, copy the new files over the old ones: BIOS, games,
    saves, settings and pads.cfg in neopi are kept and work as before.

SET-UP

  1. Use a microSD card formatted as FAT32, with a standard partition
     table.
  2. Copy EVERYTHING in this folder to the card's root.
  3. Put neogeo.zip (the MAME/FBNeo BIOS set) and your games in .neo
     format (made with TerraOnion's NeoBuilder) in the neopi folder.
  4. Connect a USB gamepad and the display (on a Pi 4, HDMI 0, next to the
     power socket). Insert the card and switch on.

START-UP

  - The screen stays black briefly, then the game list appears.
  - MV-1 mode: with only one game in neopi, the list is skipped and the
    game starts at once. GAME LIST in the in-game menu still opens the
    list.
  - Game list: Up/Down to choose, Left/Right to jump by 8, A or Start to
    play, D for OPTIONS.
  - Benchmark: an empty file named neopi/BENCH (no extension) runs one on
    the selected game before normal play: 30 seconds of the game's demo,
    then 60 seconds measured. Do not touch anything. Results in
    neopi/BENCH.TXT and neopi/PROFILE.TXT. Delete BENCH to start normally.
  - Multicore check: an empty file named neopi/MCTEST checks, on the
    selected game, that the picture drawn on several CPU cores is
    identical to the one drawn on a single core (four runs of 60 seconds).
    Results in neopi/MCTEST.TXT. Delete MCTEST to start normally.

SETTING UP THE CONTROLS

  The first time a pad model is connected, the CONTROLS screen opens by
  itself. Press the button you want for each function, in this order: A,
  B, C, D, START, SELECT/COIN. After the last one, the set-up is saved
  (SAVED).

  Directions need no set-up: the d-pad, hat and left stick always work. A
  button already assigned is ignored. On a keyboard, Esc cancels.

  The set-up applies to every pad of the same model and is stored in
  neopi/pads.cfg. To redo it: OPTIONS (D in the game list) > CONTROLS.

CONTROLS IN GAME

  D-pad or left stick       Joystick
  A, B, C, D                The NEOGEO buttons
  Start                     Start
  Select (short press)      Insert a coin (AES: the Select button)
  Select held 1 second      In-game menu (the game pauses)
  Select + A                Screenshot: neopi/shots/<game>_NNN.png
  Select + B                Lazy finger on/off: held, A to D fire
                            repeatedly (per player, off at every game
                            start)
  Select + C                Status line on/off
  Select + D                Credits (B closes them)

  The second pad connected is player 2.

IN-GAME MENU (hold Select for 1 second; B resumes the game)

  SAVE STATE     Save the game (one save per game).
  LOAD STATE     Load it (available once a save has been made).
  BIOS SETTINGS  MVS and UNIBIOS only. MVS: presses the TEST button and
                 the BIOS's own menu appears (game and cabinet settings);
                 its EXIT item restarts the game with the new settings.
                 UNIBIOS: restarts the game into the Universe BIOS menu.
  OPTIONS        The OPTIONS menu, over the paused game; B comes back
                 here. SYSTEM and REGION apply from the next game.
  RESET          Restart the game.
  GAME LIST      Back to the game list.
  POWER OFF      3-second countdown (B cancels). Everything is saved, then
                 picture and sound are switched off: you can unplug the
                 power.

OPTIONS MENU (D in the game list, or from the in-game menu)

  A or Left/Right changes a setting, B goes back. Settings are stored in
  neopi/neopi.cfg.

  SYSTEM        MVS (arcade) or AES (home console), from the next game.
                UNIBIOS too, if uni-bios_4_0.rom is in neogeo.zip.
  REGION        EUROPE / USA / JAPAN / ASIA: the BIOS region (language,
                censorship), from the next game. With UNIBIOS the region
                is set in the Universe BIOS menu.
  SCANLINES     OFF / LIGHT / MEDIUM / STRONG / COMPOSITE. COMPOSITE has no
                scanlines: blargg's NTSC filter gives the look of a
                composite video connection of the time. Each line is
                filtered as soon as it is drawn, on its own CPU core: no
                added lag. On a Pi 3 the scanlines and COMPOSITE need
                multicore: with multicore=off this item shows MULTICORE
                OFF.
  CONTROLS      Set up a pad (with several pads, press a button on the one
                to set up).
  POWER OFF     As in the in-game menu.

STATUS LINE (Select + C)

  Below the picture:

    60 FPS   LOAD 62%   39 C   DLY 6.5   1080P 4X CPU+HVS

  - LOAD: how much of each frame is used (green; yellow from 75%; red
    above 95%).
  - DLY: the frame delay in ms. With beam racing, RACE instead: the ms
    from reading the pads to sending the top of the picture to the TV.

LOW INPUT LAG

  Each frame starts with the vertical blank, when the games read the pads,
  then draws the picture: the next frame shown already answers the pads.
  No run-ahead.

  Frame delay: the frame starts a few ms after vsync, so the pads are read
  later. It is automatic (0 to 6 ms) and drops at once if the game gets
  heavier.

  Beam racing: the picture is drawn line by line just ahead of the HDMI
  output, as a real NEOGEO draws it on a CRT. At start-up neo-pi measures
  the Pi's video scaler for about a second. If lines are late too often,
  that game goes back to double buffering + frame delay (LOG.TXT says so);
  the next game started from the list tries again.

PIXEL PERFECT

  Integer scaling only (square pixels, never stretched), with black
  borders. 1080p: 1216x896 picture. 720p: 912x672 picture.

TESTING OPTIONS (in neopi/neopi.cfg, one per line)

    beamracing=off   Double buffering + frame delay (more input lag)
    framedelay=off   No frame delay
    framedelay=5     Fixed delay in ms (1 to 12; too much causes stutter)
    scaler=cpu       No hardware scaler (slower)
    multicore=off    Everything on one CPU core (slower)
    usbpoll=500      Poll pads every 2 ms instead of every 1 ms (1000 Hz)
    usbpoll=250      Every 4 ms
    usbpoll=native   At the rate the pad asks for (usually every 8-10 ms)
    resampler=speex  The previous, heavier audio resampler

  If a pad misbehaves (double or missed presses, disconnects), try
  usbpoll=500, then usbpoll=250.

FILES ON THE CARD (neopi folder)

  LOG.TXT        Diagnostics: model, HDMI, pads, audio, per-minute
                 timings. Updated when the menu opens, when the game
                 changes and at power-off
  BENCH.TXT      Benchmark results
  PROFILE.TXT    Benchmark: where the time goes (for the developer)
  MCTEST.TXT     Multicore check results
  shots/         Screenshots (Select + A)
  saves/         Save states (.st1 MVS, .sa1 AES, .su1 UNIBIOS), backup
                 memory (.nv, .unv with UNIBIOS), cartridge memory (.srm),
                 memory cards (.mcr)
  LAST.TXT       Last game played (the list starts there)
  neopi.cfg      OPTIONS settings
  pads.cfg       Controls for each pad model

  Always switch off with POWER OFF: it saves settings, save data and
  LOG.TXT. For help, LOG.TXT and BENCH.TXT are all that is needed.

IF SOMETHING GOES WRONG

  The green LED blinks a number of times, repeatedly:
  2 = HDMI   3 = SD card   4 = BIOS   5 = no .neo game   6 = emulator
  7 = memory

  A game that cannot be loaded (a damaged .neo file, or no BIOS for the
  chosen SYSTEM and REGION) shows CANNOT LOAD GAME, then the list again;
  LOG.TXT says why.

  No picture at all: put a # in front of hdmi_group and hdmi_mode in
  config.txt.

----------------------------------------------------------------------------

neo-pi is a human-led, vibe-coded experiment by Alessio Scanderebech,
written with Claude. Free software, licensed under GPLv3 (see LICENSES.txt).
Site: https://alessioscand.github.io/neo-pi/

NEOGEO is a trademark of SNK. BIOS and games are not included.
