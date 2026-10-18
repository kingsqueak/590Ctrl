# 590Ctrl

Control software for the Kenwood TS-590SG over its USB port, for macOS and
Linux. Qt 6 / C++20.

Current features: VFO A/B tuning, mode and DATA selection, AF/RF gain, squelch,
IF filter A/B with the mode-dependent high/low-cut or width/shift controls, an
S-meter, and an 18-band RX/TX equalizer editor that can program the User (U)
curve and save/load presets as JSON.

## Building

```sh
# macOS (Homebrew):  brew install qt cmake ninja
# Debian/Ubuntu:     sudo apt install qt6-base-dev libqt6serialport6-dev cmake ninja-build
cmake -S . -B build -G Ninja
cmake --build build
(cd build && ctest --output-on-failure)
```

Run `build/590Ctrl.app/Contents/MacOS/590Ctrl` (macOS) or `build/590Ctrl` (Linux).

## Connecting the radio

The TS-590SG's USB-B port carries two devices: a USB audio codec ("USB Audio
CODEC") and a Silicon Labs CP2102 serial bridge for CAT control. 590Ctrl
uses the serial bridge and picks it automatically (VID 10C4, PID EA60).

* **macOS:** use `cu.SLAB_USBtoUART`. If the Silicon Labs VCP driver is
  installed, the same CP2102 also appears as `cu.usbserial-<serial>`, which
  stays silent. 590Ctrl lists the SLAB port first.
* **Linux:** the port is `/dev/ttyUSB0` (or similar). Your user must be in the
  `dialout` group (`uucp` on Arch).
* **Speed:** must match radio menu **068** (USB port communication speed).
  The factory default is 115200 bps.
* **RTS:** the radio only sends data while RTS is asserted, so 590Ctrl holds
  RTS high and DTR low. If you've assigned RTS to PTT in the radio's menus,
  connecting will key the transmitter.

While connected, 590Ctrl turns on Auto Information (`AI2`, which the radio
doesn't save). It puts back the radio's previous AI setting when it
disconnects.

No radio handy? Pick **Simulator (no radio)** as the port.
**View → CAT Monitor** shows raw CAT traffic.

## Equalizer notes

* The EQ curve is selected per mode family (SSB, SSB-DATA, CW, FM, FM-DATA,
  AM, AM-DATA, FSK) with the `EQ` command. The editor always works on the
  family for the current mode.
* The radio keeps one User curve for RX and one for TX, shared by every mode
  family set to **U** (checked on a TS-590SG with firmware 1.07). Editing U in
  SSB also changes U in SSB-DATA, AM and the rest.
* Band levels (`UR`/`UT`) can always be read. Writing them fails while the
  curve is OFF. Per the Kenwood reference, only the **U** curve keeps them in
  the radio.
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

## Layout

| Path | Contents |
|---|---|
| `src/core/CatCodec.*` | Pure CAT command encoders/parsers and spec tables |
| `src/core/CatLink.*` | Framing, one-at-a-time command queue, error attribution, coalescing |
| `src/core/Radio.*` | Radio state model; setters, AI handling, initial sync |
| `src/core/SerialTransport.*` | QSerialPort transport and port discovery |
| `src/core/SimulatorTransport.*` | In-process fake TS-590SG for tests and offline UI work |
| `src/app/` | Qt Widgets UI |
| `tests/` | QtTest suites (codec, link, radio against the simulator) |

Protocol reference: Kenwood *TS-590S/SG PC Control Command Reference Guide*
(B5A-0316-20/01).
