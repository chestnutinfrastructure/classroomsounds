# Holly v6 hardware — PCB, pinout, toolchain

This is the **single source of truth** for v6 hardware. If anything in
the migration plan or firmware code conflicts with this file, this file
wins until updated.

---

## Board overview

The v6 board is a redesigned ESP32 PCB built by an external engineer.
Differences vs v5:

| Subsystem        | v5 (trial)                 | v6 (commercial)             |
|------------------|----------------------------|-----------------------------|
| MCU              | ESP32 dev module           | ESP32 on custom PCB         |
| LED ring         | 120-LED NeoPixel strip (2m)| Same 120-LED, dedicated rail|
| Mic              | I2S MEMS (INMP441)         | I2S MEMS (same family)      |
| BMP280           | I2C                        | I2C                         |
| microSD          | SPI                        | SPI                         |
| **Audio output** | **None**                   | **I2S amp + speaker**       |
| Programming      | USB serial (Arduino IDE)   | **ESP-Prog over JTAG**      |
| Toolchain        | Arduino IDE                | **VS Code + PlatformIO**    |
| Power for LEDs   | USB-C (overdrawn)          | **Dedicated 5V rail**       |

---

## Pinout (definitive — supersedes v5)

From the engineer, Mon 2026-06-15:

```
LED_PIN       26    NeoPixel data
BUTTON_PIN    13    User button

SD_CS          5    microSD chip-select (SPI)
SD_SCK        18    microSD clock
SD_MISO       19    microSD MISO
SD_MOSI       23    microSD MOSI

BMP_SDA       21    BMP280 I2C data
BMP_SCL       22    BMP280 I2C clock

MIC_SD        32    I2S mic data
MIC_SCK       33    I2S mic bit clock
MIC_WS        25    I2S mic word-select

AMP_BCLK      14    I2S amp bit clock         ← NEW in v6
AMP_LRCLK     27    I2S amp word-select       ← NEW in v6
AMP_DIN       16    I2S amp data              ← NEW in v6
AMP_ENABLE    17    Amp enable (HIGH = on)    ← NEW in v6
```

**Diffs from v5:**
- `LED_PIN` was `26` in v5 — unchanged.
- `MIC_SD/SCK/WS` were `32/33/25` in v5 — unchanged.
- `SD_*`, `BMP_*`, `BUTTON_PIN`, `AMP_*` are NEW or formalised here.
- v5 firmware (`Holly_v5091.ino`) has no pin defines for SD, BMP, BUTTON,
  or AMP — they were dev-board defaults or absent. v6 firmware must use
  these defines explicitly.

### Things to confirm with the engineer before firmware Phase 1

- [ ] Is `AMP_ENABLE` active-HIGH (assumed) or active-LOW?
- [ ] Is the LED rail isolated from USB-C 5V or does it share? (Decided
      separate per current draw — 7.2A worst case at 100% white reward.)
- [ ] Is the I2S amp the MAX98357A (common ESP32 pairing) or another part?
      Different parts have different gain configurations and shutdown
      behaviour.
- [ ] Is the JTAG header populated on production boards, or pads-only?

---

## Audio (I2S amp + microSD)

From the engineer, 2026-06-15:

> Just FYI, for the speaker, the gain value I used in the demo code is 3.0f
> and the audio files are named: alert1.wav, alert2.wav, alert3.wav

**Implications for firmware design:**
- Three demo audio files on the microSD: `alert1.wav`, `alert2.wav`,
  `alert3.wav`. Confirm sample rate / bit depth from the engineer; v6
  firmware should validate on load.
- Default gain `3.0f`. Make this a configurable field in the device
  registry (`audio_gain`) so admins can tune per-classroom — the same
  way `activeBrightness` is per-device in v5.
- Audio playback should be triggered by:
  - Alert state transitions (Lockdown / Invacuation / Evacuation / All
    Clear / Drill) — each alert kind maps to a specific WAV.
  - Direct dashboard "play sound" commands for testing.
- Audio MUST NOT play during normal monitoring — speakers are an
  attention event, not ambient.
- The `AMP_ENABLE` pin should be driven LOW when not playing to save
  power and reduce hiss. Drive HIGH a few ms before playback starts so
  the amp settles, then back to LOW after the file ends.

**Mapping alerts → WAVs (proposed, confirm with Dan):**
- Lockdown: TBD (likely `alert1.wav` or a dedicated lockdown.wav)
- Invacuation: TBD
- Evacuation: TBD
- All Clear: TBD
- Drill variants: same files but at reduced gain.

Don't hardcode this mapping in firmware. Pull it from device config so
schools can swap audio without re-flashing.

---

## Toolchain switch — Arduino IDE → VS Code + PlatformIO

v6 development uses **VS Code + the PlatformIO extension**, programming
the board via **ESP-Prog (JTAG)** rather than USB serial.

### Why
- Reproducible builds: `platformio.ini` pins library versions. No more
  "works on my IDE" version drift.
- Real debugging: ESP-Prog gives breakpoints, watch variables,
  single-step — impossible from Arduino IDE.
- CI: PlatformIO builds from the command line, so GitHub Actions can
  produce signed firmware `.bin` artefacts on every tag.
- Faster flashing for factory programming and field recovery.

### What stays the same
- The C++ code is the same language and the same Arduino-style API
  (`setup()`, `loop()`, `Adafruit_NeoPixel`, `WiFi.h`, etc.).
- Library names are the same — they're just pinned in `platformio.ini`
  via `lib_deps` instead of installed globally via Library Manager.

### What changes structurally
- Single `.ino` file → `src/main.cpp` with proper headers in `src/`
  and local libraries in `lib/`.
- Project root has `platformio.ini`.
- Build artefacts go to `.pio/build/<env>/firmware.bin`.

### Target `platformio.ini` skeleton (Phase 1 work, don't write yet)

```ini
[platformio]
default_envs = esp32_v6

[env:esp32_v6]
platform = espressif32
board = esp32dev          ; confirm exact board family with engineer
framework = arduino
monitor_speed = 115200
upload_protocol = esp-prog
debug_tool = esp-prog
build_flags =
    -D LED_PIN=26
    -D BUTTON_PIN=13
    -D SD_CS=5
    -D SD_SCK=18
    -D SD_MISO=19
    -D SD_MOSI=23
    -D BMP_SDA=21
    -D BMP_SCL=22
    -D MIC_SD=32
    -D MIC_SCK=33
    -D MIC_WS=25
    -D AMP_BCLK=14
    -D AMP_LRCLK=27
    -D AMP_DIN=16
    -D AMP_ENABLE=17
    -D AMP_GAIN_DEFAULT=3.0f
lib_deps =
    adafruit/Adafruit NeoPixel @ ^1.12.0
    adafruit/Adafruit BMP280 Library @ ^2.6.8
    knolleary/PubSubClient @ ^2.8
    bblanchon/ArduinoJson @ ^7.0.0
    ; I2S audio lib — pick one and pin: ESP32-audioI2S or similar
```

Pin library versions when the firmware build actually starts. Don't
hand-roll this in advance.

---

## Power architecture

The 120-LED NeoPixel strip can draw up to **7.2A** at 100% white
brightness (which the quiet-reward animation triggers). USB-C standard
spec is 3A; USB-PD is 5A — neither is enough.

**Design rule:** LED rail is on its own 5V supply rated ≥ 8A (gives
headroom). USB-C feeds the ESP32 logic only. Common ground between the
two rails. The amp shares the logic rail unless current draw justifies
its own.

This was discussed and the engineer is implementing it. Carson does not
need to design the power tree — just respect it: never assume LED
brightness is software-limited below 100%, never assume USB-C can carry
the load.

---

## Engineer comms log

Keep this section appended-only so future Carson sessions have continuity.

### 2026-06-15 — engineer drop-off plan + pinout + audio

> Hi Dan, The board is tested and working, with audio file outputs from a
> microSD card and peripherals all performing as expected. It'll have a
> demo sketch installed to demonstrate functionality. You will need to
> use Visual Studio Code with the platformio plugin to write the code to
> ESP32 via the esp-prog instead of Arduino IDE, it uses the same code
> format and libraries although some may need to be installed if you've
> not used it before. I can include some documentation/guidance on this
> if required. I was hoping you may be available Wednesday the 17th at
> some point late morning/early afternoon for me to drop off the board?

Plus the pinout list and audio gain shown above.

### 2026-06-15 — earlier engineer hardware questions

Three engineer questions answered:
1. **JTAG:** Include it. Factory programming, field recovery, and
   debugging all benefit. Cost delta is minimal (header + traces).
2. **LED brightness:** Strip runs at `activeBrightness` (default 100% in
   v5). Quiet reward animation renders white at full brightness, so 7.2A
   for 120 LEDs is real, not theoretical. Hence dedicated 5V rail for
   the LED strip (≥ 8A), USB-C for logic only.
3. **120 LEDs / 2m:** confirmed; `#define NUM_LEDS 120` in v5 firmware.

### Next steps for hardware
- Wed 17 Jun 2026: engineer drops off the board. Ask him to bring
  ESP-Prog + cables, do a hello-world flash in person, and send a
  starter `platformio.ini` matched to the board.
- Confirm the open questions in the pinout section above before Phase 1
  firmware work begins.
