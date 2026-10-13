# Trust GXT 940 Xidon — USB HID Configuration Protocol

This document describes the messages the GXT 940 Xidon gaming mouse accepts over USB to read
and change its stored settings. It is written for anyone who wants to support this mouse on
other operating systems.

## How this specification was made

The mouse ships with configuration software for Windows only. To make the mouse usable on
Linux, the way that software communicates with the mouse was studied for the sole purpose of
interoperability. The findings were written up as this functional specification, describing
only message formats, field positions and values.

OpenXidon was then written from scratch from this specification. It contains no code, text,
images or other material from the official software. The behaviour described here was
checked against a real GXT 940 where marked **(confirmed)**.

## Status of this document

- **(confirmed)** means the behaviour was observed on a real mouse.
- *(unverified)* marks inferences that have not been checked on hardware.
- Everything else is believed correct but has not been individually confirmed.

## 1. Device

| Field | Value |
|---|---|
| USB VID:PID | `145F:02A2` **(confirmed)** |
| USB manufacturer string | `SINOWEALTH` **(confirmed)** |
| Sensor | PixArt PMW3325, reported as sensor code `0x0B` **(confirmed)** |
| Onboard modes | 3 |

The mouse has two USB interfaces. Interface 0 is the standard mouse. Interface 1 carries a
keyboard collection, a consumer-control collection, and the **vendor collection (usage page
`0xFF00`, usage `0x01`)** used for configuration **(confirmed)**:

| Report ID | Type | Size incl. ID | Purpose |
|---|---|---|---|
| 5 | Feature | 6 bytes | Command channel |
| 4 | Feature | 520 bytes | Data channel |
| 7 | Input | 8 bytes | Notifications from the mouse |

On Linux all three reports are on the same hidraw node (interface 1) **(confirmed)**.

## 2. Transport

### 2.1 Command report (feature report 5)

```
byte  meaning
 0    0x05  report ID
 1    command
 2-5  arguments (0 when unused)
```

The reply is read with a GET_FEATURE of report 5. It echoes the command in byte 1, followed by
the reply data **(confirmed)**.

### 2.2 Data report (feature report 4)

```
byte   meaning
 0     0x04  report ID
 1     sub-command
 2     argument A
 3     argument B (usually the payload length)
 4-7   0
 8-519 payload
```

When reading, the mouse echoes the sub-command in byte 1, and the payload starts at byte 8
**(confirmed)**. A GET_FEATURE of report 4 with no preceding request is answered with a USB
stall **(confirmed)**.

### 2.3 Notification report (input report 7)

Sent when the user changes mode or DPI with the mouse's buttons **(confirmed)**:

| Byte | Meaning |
|---|---|
| 0 | `0x07` report ID |
| 1 | Active mode (1–3) |
| 2 | Low nibble: active DPI level, counting enabled levels only, starting at 1. High nibble: unknown. |
| 3 | Active DPI code (§4.1) |
| 4–7 | Unknown |

Example: `07 01 B2 0F 00 00 00 00` means mode 1, level 2, 1600 DPI **(confirmed)**.

### 2.4 Quirk: spurious protocol errors

The mouse sometimes fails the status stage of a SET_FEATURE request even though it has carried
out the command. On Linux this appears as `EPROTO` ("Protocol error") **(confirmed)**. Clients
should not treat this error as a failure on its own. They should verify the result instead,
for example by reading the reply or reading the settings back.

### 2.5 Timing

Wait about 50 ms between a command and reading its reply. Leave about 20 ms between
consecutive writes.

## 3. Commands

| Command | Bytes sent | Reply | Purpose |
|---|---|---|---|
| Read ID | `05 01 00 00 00 00` | report 5: bytes 2–5 = a 4-byte device identifier **(confirmed)** | Identification |
| Get mode | `05 02 00 00 00 00` | report 5: byte 2 = active mode 1–3 **(confirmed)** | Read the active mode |
| Set mode | `05 02 MM 00 00 00` | none | Switch to mode `MM` (1–3) **(confirmed)** |
| Read settings | `05 S1 00 00 00 00`, then read report 4 | 512-byte settings block | `S1` = `0x11`, `0x21`, `0x31` for mode 1, 2, 3 |
| Write settings | `04 S1 00 7B 00 00 00 00` + 0x8C bytes | none | Write a mode's settings block **(confirmed for mode 1)** |
| Write buttons | `04 S2 00 50 00 00 00 00` + 80 bytes | none | `S2` = `0x12`, `0x22`, `0x32` |
| Write macro | `04 30 02 00 00 00 00 00 NN` + up to 510 bytes | none | Store a macro in slot `NN` (1–8) |

Reading the mode 1 settings block works **(confirmed)**. Reading modes 2 and 3 has been seen
to return an all-zero block or a stall. This is still being investigated, and may be a
timing issue.

## 4. Settings block

The settings block is changed with **read-modify-write**: read the block (Read settings),
change only the fields you need, and write back the first 0x8C bytes (Write settings). Bytes
you don't understand should be preserved.

Offsets below are positions in the payload (wire byte = offset + 8).

| Offset | Size | Field |
|---|---|---|
| `0x00` | 1 | `0x64` |
| `0x01` | 1 | Bits 0–5: sensor code, read-only (`0x0B` = PMW3325) **(confirmed)** |
| `0x02` | 1 | Bits 0–3: polling rate, `1`=125 Hz, `2`=250 Hz, `3`=500 Hz, `4`=1000 Hz **(confirmed)**. Bit 7: separate X/Y DPI **(confirmed)**. Other bits: preserve. |
| `0x03` | 1 | Low nibble: number of enabled DPI levels. High nibble: active DPI level, counting enabled levels only, starting at 1 **(confirmed)** |
| `0x04` | 1 | Bitmask of **disabled** DPI slots, bit *n* = slot *n* (0–7) **(confirmed)** |
| `0x05` | 16 | DPI codes for slots 0–7: one byte per slot, or two bytes (X then Y) when `0x02` bit 7 is set **(confirmed)** |
| `0x15` | 24 | DPI indicator colors, 8 × (R, G, B) |
| `0x2D` | 1 | Lighting effect (§4.2) **(confirmed)** |
| `0x2E`–`0x70` | | Lighting effect parameters (§4.2) |

Unused DPI slots read back as code `0`.

### 4.1 DPI codes (PMW3325)

| Code | DPI |
|---|---|
| `0x01`–`0x31` (1–49) | 200–5000 in steps of 100: DPI = 100 × (code + 1) |
| `0x32`–`0x3B` (50–59) | 5500–10000 in steps of 500: DPI = 5000 + 500 × (code − 49) |

The factory DPI codes `07 0F 17 1F 27 31` decode to 800, 1600, 2400, 3200, 4000 and 5000
**(confirmed)**.

### 4.2 Lighting

Speed and brightness are 4-bit values. Effects that take colors store the selected colors
packed one after another as R, G, B.

| ID | Effect | Parameter bytes |
|---|---|---|
| 0 | Off | none |
| 1 | Rainbow Wave | `0x2E` = brightness << 4 \| speed; `0x2F` = direction (0/1) |
| 2 | Static | `0x30` high nibble = brightness (preserve the low nibble); `0x31`–`0x33` = color |
| 3 | Breathing | `0x34` = brightness << 4 \| speed; `0x35` = color count; `0x36`–`0x4A` = up to 7 colors |
| 4 | Colorful Tail | `0x4B` = brightness << 4 \| speed |
| 5 | Neon | `0x4C` = brightness << 4 \| speed |
| 6 | Rainbow | `0x4D` high nibble = brightness; `0x4E`–`0x65` = 8 colors |
| 7 | Flicker | `0x66` = brightness << 4 \| speed; `0x67`–`0x6C` = 2 colors |
| 8 | Tail | `0x6D` = brightness << 4 \| speed |
| 9 | Wave | `0x6E` = brightness << 4 \| speed |
| 10 | Response | `0x6F` low nibble = speed |
| 11 | Streaming | `0x70` low nibble = speed |

Changing the effect, colors, speed and brightness works **(confirmed)**. The exact visual
range of the speed and brightness values is *(unverified)*.

## 5. Button map

The Write buttons payload is **20 × 32-bit little-endian action codes**, one per button slot.
An unused slot is `0x00000150` (disabled). The mouse cannot report its current map.

Factory layout:

| Slot | Function | Slot | Function |
|---|---|---|---|
| 1 | Left click | 6 | DPI + |
| 2 | Right click | 7 | DPI − |
| 3 | Middle click | 8 | Polling-rate switch |
| 4 | Back | 9 | Polling-rate switch |
| 5 | Forward | 10–20 | Disabled |

Which physical button belongs to slots 4, 5, 8 and 9 is *(unverified)*.

### 5.1 Action codes

Below, the code is shown as a 32-bit number, then as bytes `b0 b1 b2 b3` where noted.

| Action | Code |
|---|---|
| Left / right / middle click | `0x0111` / `0x0211` / `0x0411` |
| Back / forward | `0x0811` / `0x1011` |
| Double / triple click | `0x02320131` / `0x03320131` |
| Scroll up / down | `0x0112` / `0xFF12` |
| Scroll left / right | `0xFF13` / `0x0113` |
| Disabled | `0x0150` |
| DPI + / − / cycle | `0x0141` / `0x0241` / `0x0041` |
| DPI + / − (small step) | `0x0143` / `0x0243` |
| DPI lock | `b0=0x42`, `b1` = DPI code |
| Lighting on/off | `0x0250` |
| Mode switch | `0x0650` |
| Polling-rate switch | `0x0450` |
| Effect switch | `0x0750` |
| Key / key combination | `b0=0x21`, `b1` = modifier mask, `b2` = key, `b3` = second key (0 if none) |
| Multimedia | `b0=0x22`, `b1`–`b3` = bitmask (below) |
| Macro | `b0=0x70`, `b1` = macro slot, `b2` = play mode (1, 2 or 4) *(unverified meaning)*, `b3` = repeat count (≥ 1) |
| Repeated key ("fire") | `b0` = `0x31` mouse button / `0x32` key / `0x33` modifier, `b1` = target, `b2`/`b3` = repeat count and interval *(order unverified)* |

Keys are standard USB HID keyboard usage codes.

**Modifier mask:** LCtrl `0x01`, LShift `0x02`, LAlt `0x04`, LWin `0x08`, RCtrl `0x10`,
RShift `0x20`, RAlt `0x40`, RWin `0x80` (the standard HID modifier byte).

**Mouse buttons:** left `0x01`, right `0x02`, middle `0x04`, back `0x08`, forward `0x10`.

**Multimedia bitmask:**

| Byte | Bits |
|---|---|
| b1 | `01` next track, `02` previous track, `04` stop, `08` play/pause, `10` mute, `40` volume up, `80` volume down |
| b2 | `01` media player, `02` file browser, `10` email, `20` calculator |
| b3 | `01` search, `02` browser home, `04` browser back, `08` browser forward, `10` browser stop, `20` refresh, `40` favorites |

## 6. Macros

The macro data (sent at wire byte 9 of the Write macro report):

```
bytes 0-1  number of events, big-endian
then 3 bytes per event:
  byte 0   bit 7: press (1) / release (0), for buttons and keys only
           bits 4-6: event type
           bits 0-3: delay, bits 8-11
  byte 1   delay, bits 0-7, in milliseconds (a value of 0 is sent as 1)
  byte 2   code
```

| Type | Event | Code |
|---|---|---|
| 1 | Mouse button | mouse button bit (above) |
| 2 | Move X | signed 8-bit |
| 3 | Move Y | signed 8-bit |
| 4 | Wheel | signed 8-bit |
| 5 | Key | HID keyboard usage |
| 6 | Modifier | Ctrl `01`, Shift `02`, Alt `04`, Win `08` |

A macro holds up to 168 events. Macro slots are 1–8. A button plays a macro through the macro
action code in §5.1.

## 7. Open questions

- Reliable reading of modes 2 and 3.
- Settings-block byte `0x00` and the bytes after `0x70`.
- Notification bytes 2 (high nibble) and 4–7.
- Visual range of speed and brightness; meaning of the macro play modes; order of the
  repeated-key parameters.
- Which physical buttons correspond to slots 4, 5, 8 and 9.
