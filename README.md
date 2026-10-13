# OpenXidon

An open-source Linux configuration tool for the **Trust GXT 940 Xidon** gaming mouse.

> **Unofficial project.** OpenXidon is not affiliated with, endorsed by, or supported by
> Trust International B.V. or SinoWealth. Read the [Disclaimer](#disclaimer) before use.

> [!WARNING]
> **Use at your own risk.** Trust's warranty terms state that the warranty is void in cases of
> "modification" or use "for other purpose than its intended use". Using OpenXidon to change
> your mouse's settings **may void your warranty**, and the authors accept no responsibility
> for any damage, malfunction or loss of warranty. If you want to keep your warranty safe,
> use only the official software.

## Objective

The GXT 940 stores its settings (DPI levels, lighting, polling rate, button mapping and
macros) in the mouse itself, but the official configuration software only runs on Windows.
On Linux the mouse works as a plain mouse, and none of its settings can be changed.

OpenXidon fills that gap. It is a small, dependency-light command-line tool that talks to the
mouse directly over USB HID. It lets Linux users change the same settings the official
software changes, so the hardware they bought can be fully used on their operating system of
choice.

The project exists purely for **interoperability**. It contains no code, images, text or
other files from the official software, and it never modifies the mouse's firmware.

## Status

Tested on a real GXT 940 (USB `145f:02a2`, PMW3325 sensor):

| Feature | Status |
|---|---|
| Device detection, reading settings | Working |
| Lighting effects, colors, speed, brightness | Working |
| DPI levels, colors, current level, X/Y DPI | Working |
| Polling rate | Working (confirmed by read-back) |
| Switching onboard mode | Working (confirmed by read-back) |
| Live mode/DPI events from the mouse | Working |
| Button remapping | The mouse accepts the writes; physical behaviour not yet fully confirmed |
| Macros | The mouse accepts the writes; physical behaviour not yet fully confirmed |
| Reading onboard modes 2 and 3 | Under investigation; writes to a mode that cannot be read are blocked |

Only the GXT 940 Xidon has been tested. Other mice, even ones built on the same chip, are not
supported.

## Building

Requirements:
- a C++20 compiler (GCC 11+ or Clang 14+)
- CMake 3.16+
- hidapi (`hidapi` on Arch, `libhidapi-dev` on Debian/Ubuntu, `hidapi-devel` on Fedora)
- pkg-config

```sh
cmake -S . -B build
cmake --build build
```

The program is built as `build/openxidon`. To install it system-wide:

```sh
sudo cmake --install build
```

This installs the program only. Set up permissions separately, as described below.

## Permissions

By default only root can talk to the mouse. To use OpenXidon as a normal user, install the
included udev rule and replug the mouse:

```sh
sudo cp udev/70-openxidon.rules /etc/udev/rules.d/
sudo udevadm control --reload
sudo udevadm trigger
```

Without the rule, prefix the commands below with `sudo`.

## Quick start

```sh
openxidon info                       # show the current settings
openxidon dump 1 > mode1-backup.hex  # back up mode 1 before changing anything
openxidon light                      # pick a lighting effect interactively
openxidon light static orange        # solid orange
openxidon light off                  # LEDs off
openxidon dpi 1 set 2 1200           # DPI level 2 of mode 1 to 1200
openxidon polling 1 1000             # 1000 Hz polling on mode 1
openxidon restore 1 mode1-backup.hex # put mode 1 back exactly as it was
```

Run `openxidon --help` for the full command list, and add `--dry-run` to any command to see
the exact bytes it would send without touching the mouse.

## Usage

### Onboard modes

The mouse holds **3 onboard modes**, each with its own DPI, polling and lighting settings. The
mode button on the mouse switches between them.

```sh
openxidon mode        # show the active mode
openxidon mode 2      # switch to mode 2
```

Most commands take a mode number (`1`, `2`, `3`) or `all`. The `light` command uses the
active mode when you don't give one.

### Lighting

```sh
openxidon effects                          # numbered list of effects and their options
openxidon light                            # interactive picker
openxidon light breathing red blue speed=8
openxidon light 3 purple                   # effects can be chosen by number
openxidon light next                       # cycle through the effects (or: prev)
openxidon light all rainbow-wave direction=1
openxidon light off
```

| # | Effect | Options |
|---|---|---|
| 1 | Rainbow Wave | speed, brightness, direction |
| 2 | Static | brightness, 1 color |
| 3 | Breathing | speed, brightness, up to 7 colors |
| 4 | Colorful Tail | speed, brightness |
| 5 | Neon | speed, brightness |
| 6 | Rainbow | brightness, 8 colors |
| 7 | Flicker | speed, brightness, 2 colors |
| 8 | Tail | speed, brightness |
| 9 | Wave | speed, brightness |
| 10 | Response | speed |
| 11 | Streaming | speed |
| 12 | Off | |

Colors can be given as `RRGGBB` hex or by name: red, green, blue, white, yellow, cyan, magenta,
orange, purple, pink, black. Speed and brightness take values from 0 to 15. Settings you don't
mention keep their current values.

### DPI

The PMW3325 supports 200–5000 DPI in steps of 100, and 5500–10000 in steps of 500. Each mode
has 8 DPI slots (the official software uses 6).

```sh
openxidon dpi 1 set 1 800          # slot 1 to 800 DPI
openxidon dpi 1 set 1 800x1600     # separate X and Y DPI
openxidon dpi 1 current 2          # make slot 2 the active level
openxidon dpi 1 disable 6          # skip slot 6 when cycling with the DPI button
openxidon dpi 1 color 1 00FF00     # LED color shown for slot 1
openxidon dpi 1 xy off             # link X and Y again
```

### Polling rate

```sh
openxidon polling all 1000         # 125, 250, 500 or 1000 Hz
```

### Buttons

The mouse cannot report its current button map, so every `buttons` command writes a full map:
the factory layout, plus the changes you list.

```sh
openxidon buttons 1                              # factory layout
openxidon buttons 1 5=key:a 4=media:volume-up
openxidon buttons all 4=combo:ctrl+c 5=combo:ctrl+v
openxidon functions                              # list every function and key name
```

Available functions include `left`, `right`, `middle`, `back`, `forward`, `double-click`,
`triple-click`, `scroll-up`, `scroll-down`, `dpi-up`, `dpi-down`, `dpi-loop`, `mode-next`,
`rate-next`, `effect-next`, `light-toggle`, `disable`, `key:<key>`, `combo:<mods>+<key>`,
`media:<name>`, `dpi-lock:<dpi>`, `macro:<slot>[:<repeat>]` and
`fire:<target>:<repeat>:<interval>`.

### Macros

Macros are plain text files, one action per line, each with an optional delay in milliseconds:

```
down ctrl 20
tap c 30
up ctrl
move x -5
wheel 1
tap mouse:left 10
```

Upload a macro to a slot (1–8), then bind it to a button:

```sh
openxidon macro 1 copy.txt
openxidon buttons 1 5=macro:1
```

### Backup, restore and troubleshooting

```sh
openxidon dump 1 > mode1.hex       # save a mode's raw settings
openxidon restore 1 mode1.hex      # write them back
openxidon listen                   # print mode/DPI changes as you press buttons
openxidon list                     # show the USB interfaces found
openxidon probe                    # read-only connection test
```

## How it works

The mouse exposes a vendor-defined HID interface with three reports:

| Report | Type | Size | Purpose |
|---|---|---|---|
| 5 | Feature | 6 bytes | Short commands: select a mode, query the active mode, request a settings block |
| 4 | Feature | 520 bytes | Bulk data: a mode's settings block, button map, or macro |
| 7 | Input | 8 bytes | Notifications from the mouse when its buttons change mode or DPI |

Changing a setting means asking the mouse for the current settings block of a mode, changing
only the relevant bytes, and writing the block back. OpenXidon works the same way: it never
writes a block it could not read and validate first.

Some units occasionally report a USB "protocol error" for a command they have in fact carried
out. When that happens, OpenXidon checks the result by reading it back instead of failing.

## Safety notes

- **Back up first.** Run `openxidon dump <mode> > file` for each mode before experimenting.
  `restore` can then put a mode back byte for byte.
- **Settings are stored in the mouse.** Every change is written to the mouse's internal memory,
  which tolerates a limited number of writes. Normal configuration is fine. Do not script
  OpenXidon to change settings continuously, for example in a fast loop or a music visualiser.
- **No firmware changes.** OpenXidon only changes user-configurable settings. It never reads,
  writes or updates the mouse firmware.
- **Safety checks.** Writes are refused if the settings read from the mouse do not look valid
  (wrong sensor, empty block). Do not use `--force` unless you know why the check failed.

## Disclaimer

**Unofficial software.** OpenXidon is an independent project. It is not affiliated with,
authorised, endorsed or supported by Trust International B.V., SinoWealth, or any of their
affiliates. Do not contact Trust support about this software.

**Trademarks.** "Trust", "GXT" and "Xidon" are trademarks of their respective owners. They are
used here only to identify the hardware this software is compatible with.

**No warranty.** This software is provided "as is", without warranty of any kind, express or
implied, including but not limited to the warranties of merchantability, fitness for a
particular purpose and non-infringement. In no event shall the authors or contributors be
liable for any claim, damages or other liability, including damage to your mouse, loss of
data or loss of settings, arising from the use of this software.

**Your mouse's warranty: use at your own risk.** The manufacturer's warranty terms exclude, among other things,
"modification", "using the product for other purpose than its intended use", and
"incompatibility with other hardware/software not defined as minimum system requirements". The
manufacturer may treat the use of third-party configuration software, or use on an operating
system it does not support, as falling under these exclusions. OpenXidon changes only the same
user-configurable settings as the official software and does not modify firmware, but by using
it **you accept any risk to your device and its warranty**. If in doubt, use the official
software.

**Interoperability.** OpenXidon was developed independently, for the sole purpose of making
the mouse usable on operating systems the official software does not support. It contains no
code, assets, text or other material from the official software, and no proprietary files are
distributed with it.

**Use at your own risk.** You are responsible for complying with the laws and any agreements
that apply to you. Nothing in this repository is legal advice.

## License

OpenXidon is released under the [MIT License](LICENSE).
