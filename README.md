# 590Ctrl

A desktop control panel for the **Kenwood TS-590SG** over its USB port, for
macOS and Linux. Written in C++20 with Qt 6.

![590Ctrl connected to a TS-590SG](docs/screenshot.png)

Version 0.10 · Licensed under the [GPL-3.0](LICENSE).

## Features

**Tuning and modes**
- VFO A and B displays: scroll over any digit to tune it (one step per wheel
  click), or double-click to type a frequency
- RX/TX VFO selection, split, A=B
- Mode buttons (LSB, USB, CW, CW-R, FSK, FSK-R, FM, AM) and DATA
- RIT / XIT with a signed offset and CLEAR
- **Bands** tab: 1.8–50 MHz and GENE; click the current band again to step
  through its band memories

**Receive**
- AF gain, squelch, RF gain with an ATT → PRE → off button
- NB (NB1 / NB2 / both) and NR (NR1 / NR2), each with its level
- AGC off / slow / fast, with the time constant for the active mode
- IF filter A/B with the mode-dependent high/low-cut or width/shift controls
- Auto and manual notch, with bandwidth and position
- Antenna: ANT1/ANT2, RX ANT, DRV, and the internal tuner (AT in line, ATun to
  run a tune cycle)

**Transmit**
- TX power, tune power, carrier level, mic gain, VOX and VOX level
- Speech processor with input and output levels
- TX monitor with level
- TX filter low/high cut for SSB/AM and SSB-DATA
- SEND (latching), TUN (latching tune carrier at tune power) and a momentary TX
  button. The app always releases transmit before disconnecting or quitting.

**Meter**
- S-meter with S1–S9 / +20–+60 dB scale, smooth color zones, peak hold and an
  RF gain marker
- On transmit: power in watts, SWR, speech-processor compression in dB, and ALC

**Equalizer**
- 18-band RX and TX editors for the radio's EQ, including the User (U) curve
- Named presets (save, apply, import/export as JSON) and per-mode auto-load
  into the User curve; see [Equalizer notes](#equalizer-notes)

**Radio and app**
- Power the radio on and off from the app, including waking a radio that was
  switched off at its front panel
- USB audio in/out levels
- Auto-detects the radio's USB serial port
- Built-in simulator for trying the app without a radio
- CAT monitor (**View → CAT Monitor**) showing raw radio traffic
- **View → Style** (macOS, Fusion, …) and **View → Appearance** (System, Light,
  Dark)

## Installing on macOS

Download `590Ctrl-0.10.dmg` from the
[Releases](https://github.com/kingsqueak/590Ctrl/releases) page, open it, and
drag **590Ctrl** to **Applications**. It runs on macOS 26.0 or later, on both
Apple silicon and Intel Macs.

The app isn't notarized by Apple, so the first launch is blocked. Open it
once, then go to **System Settings → Privacy & Security** and click **Open
Anyway**. Alternatively, run:

```sh
xattr -dr com.apple.quarantine /Applications/590Ctrl.app
```

## Building from source

Requires **Qt 6.8 or newer** (Core, Widgets, SerialPort), CMake 3.21+, Ninja
and a C++20 compiler.

### macOS

```sh
brew install qt cmake ninja
git clone https://github.com/kingsqueak/590Ctrl.git
cd 590Ctrl
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
(cd build && ctest --output-on-failure)   # optional: run the tests
open build/590Ctrl.app
```

Homebrew's Qt is built for the macOS version it's installed on, so an app
bundled from it only runs on that version or newer. To make a DMG for older
macOS versions, build against the official Qt binaries instead (see
[Making a DMG](#making-a-dmg)).

### Linux

Qt 6.8 or newer ships with Debian 13, Ubuntu 25.04+, Fedora 41+ and Arch.
Install the build dependencies:

```sh
# Debian / Ubuntu
sudo apt install build-essential cmake ninja-build git qt6-base-dev libqt6serialport6-dev

# Fedora
sudo dnf install gcc-c++ cmake ninja-build git qt6-qtbase-devel qt6-qtserialport-devel

# Arch
sudo pacman -S base-devel cmake ninja git qt6-base qt6-serialport
```

Then build and run:

```sh
git clone https://github.com/kingsqueak/590Ctrl.git
cd 590Ctrl
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
(cd build && ctest --output-on-failure)   # optional: run the tests
./build/590Ctrl
```

Your user needs access to the serial port: add it to the `dialout` group
(`uucp` on Arch), then log out and back in.

```sh
sudo usermod -aG dialout "$USER"
```

### Making a DMG

The release DMG is built against the official Qt binaries, which are
self-contained and support older macOS versions. One way to get them is
[aqtinstall](https://github.com/miurahr/aqtinstall):

```sh
python3 -m venv aqtenv && ./aqtenv/bin/pip install aqtinstall
./aqtenv/bin/aqt install-qt mac desktop 6.12.0 clang_64 -m qtserialport -O ~/Qt

cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
      -DCMAKE_PREFIX_PATH=~/Qt/6.12.0/macos \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build-release
~/Qt/6.12.0/macos/bin/macdeployqt build-release/590Ctrl.app -dmg
```

## Connecting the radio

The TS-590SG's USB-B port carries two devices: a USB audio codec ("USB Audio
CODEC") and a Silicon Labs CP2102 serial bridge for CAT control. 590Ctrl
uses the serial bridge and picks it automatically (VID 10C4, PID EA60).

* **macOS:** use `cu.SLAB_USBtoUART`. If the Silicon Labs VCP driver is
  installed, the same CP2102 also appears as `cu.usbserial-<serial>`, which
  stays silent. 590Ctrl lists the SLAB port first.
* **Linux:** the port is `/dev/ttyUSB0` (or similar).
* **Speed:** must match radio menu **068** (USB port communication speed).
  The factory default is 115200 bps.
* **RTS:** the radio only sends data while RTS is asserted, so 590Ctrl holds
  RTS high and DTR low. If you've assigned RTS to PTT in the radio's menus,
  connecting will key the transmitter.

While connected, 590Ctrl turns on Auto Information (`AI2`, which the radio
doesn't save), so changes made on the radio show up in the app. It puts back
the radio's previous AI setting when it disconnects.

The power button next to **Connect** turns the radio on and off. If the radio
was switched off at its front panel, pressing it while disconnected wakes the
radio and connects.

No radio handy? Pick **Simulator (no radio)** as the port.

## Equalizer notes

* The EQ curve is selected per mode family (SSB, SSB-DATA, CW, FM, FM-DATA,
  AM, AM-DATA, FSK). The editor always works on the family for the current
  mode.
* The radio keeps one User curve for RX and one for TX, shared by every mode
  family set to **U** (checked on a TS-590SG with firmware 1.07). Editing U in
  SSB also changes U in SSB-DATA, AM and the rest.
* Band levels can always be read, but writing them fails while the curve is
  OFF. Per the Kenwood reference, only the **U** curve keeps them in the radio.
* TX EQ is fixed OFF in CW and FSK.

### Presets and auto-load

Each EQ panel has a preset library: **Save As…** stores the current levels
under a name, **Apply** writes the selected preset to the radio, and
**More → Import/Export** moves presets in and out as JSON files. Presets live
in the app data directory (`~/Library/Application Support/590Ctrl/590Ctrl/eq-presets`
on macOS, `~/.local/share/590Ctrl/590Ctrl/eq-presets` on Linux).

The radio has only one User curve, so **Auto-load into U** gives each mode
family its own curve anyway. Link a preset to the current mode family (e.g.
"Speech" for SSB, "Data RX" for SSB-DATA). When you enter that mode and its
curve is U, 590Ctrl writes the preset into the User curve. Choosing
**(none)** leaves U as it is.

## Meter calibration

The radio reports its meters as 0–30 display dots. 590Ctrl converts them using
points read off a TS-590SG's display:

| Meter | Calibration |
|---|---|
| S-meter | S1, S3, S5, S7, S9 every 3 dots (S9 = 15), then +20/+40/+60 dB every 5 |
| Power | 5 dots = 10 W, 10 = 25 W, 15 = 50 W, 30 = 100 W |
| SWR | 5 dots = 1.5, 10 = 2.0, 15 = 3.0 (shown as ">3" above that) |
| COMP | 1 dB per dot |
| RF gain marker | Linear, not yet calibrated |

## Project layout

| Path | Contents |
|---|---|
| `src/core/CatCodec.*` | CAT command encoders/parsers, spec tables, meter calibration |
| `src/core/CatLink.*` | Framing, one-at-a-time command queue, error attribution, coalescing |
| `src/core/Radio.*` | Radio state model: setters, AI handling, power, initial sync |
| `src/core/EqPresetLibrary.*`, `EqAutoLoader.*` | EQ presets and per-mode auto-load |
| `src/core/SerialTransport.*` | QSerialPort transport and port discovery |
| `src/core/SimulatorTransport.*` | In-process fake TS-590SG for tests and offline use |
| `src/app/` | Qt Widgets UI |
| `tests/` | QtTest suites (codec, link, radio and presets against the simulator) |

Protocol reference: Kenwood *TS-590S/SG PC Control Command Reference Guide*
(B5A-0316-20/01).

## License

Copyright (C) 2026 KC2RGW Chris Esser

590Ctrl is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

590Ctrl is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the [GNU General Public License](LICENSE) for more
details.

590Ctrl uses [Qt](https://www.qt.io/), which is licensed under the GNU LGPL v3.
