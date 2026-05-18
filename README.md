# Missile Command on ESP32-S3 + ST7789V

<img width="1600" height="1139" alt="overview" src="https://github.com/user-attachments/assets/724ff416-901e-45b8-a31b-06c41ccd0f87" />


This folder contains the ESP32 target for Missile Command.

The default pin mapping matches the current values in `platformio.ini` and `src/hal_esp32.cpp`.

## Hardware

- MCU: ESP32-S3 DevKitC-1
- Display: ST7789V SPI TFT (240x320)
- Display mode: rotated to landscape (game renders at 320x240)

## Image Assets (ESP32)

The ESP32 renderer now uses the three image headers in `src/`:

- `background-1.h`
- `civilian-target.h`
- `bunker-1.h`

Expected RGB565 dimensions:

| Asset | Pixels | Notes |
|---|---:|---|
| `background-1.h` | 320x240 | Full-screen background, drawn in `hal_clear()` |
| `civilian-target.h` | 20x20 | Drawn centered on city position |
| `bunker-1.h` | 32x20 | Drawn centered on battery position |

Transparency key for sprites is `0x0000` (pure black in RGB565), which is skipped during drawing.

## Wiring (default)

| ST7789V Pin | ESP32-S3 Pin | Notes |
|---|---:|---|
| VCC | 3V3 | Use 3.3V logic display module |
| GND | GND | Common ground |
| SCL / SCK / CLK | GPIO12 | SPI clock (`TFT_SCLK`) |
| SDA / MOSI / DIN | GPIO11 | SPI MOSI (`TFT_MOSI`) |
| CS | GPIO10 | Chip select (`TFT_CS`) |
| DC / A0 | GPIO9 | Data/command (`TFT_DC`) |
| RST / RES | GPIO13 | Reset (`TFT_RST`) |
| BL / LED | GPIO8 | Backlight enable (`TFT_BL`) |
| MISO / SDO | Not connected | Not required for this project |

## Joystick Wiring (Dual-axis XY, 5-pin)

Example mapping for a common module labeled `GND`, `+5V`, `VRx`, `VRy`, `SW`:

| Joystick Pin | ESP32-S3 Pin | Notes |
|---|---:|---|
| GND | GND | Common ground |
| +5V | 3V3 | Power from 3.3V on ESP32-S3 |
| VRx | GPIO4 | X-axis analog input (`JOY_X_PIN`) |
| VRy | GPIO5 | Y-axis analog input (`JOY_Y_PIN`) |
| SW | GPIO7 | Optional push switch, active-low (`FIRE_PIN`) |

## MAX98357A I2S Audio Amplifier (optional)

To enable WAV audio playback of game sounds:

| MAX98357A Pin | ESP32-S3 Pin | Signal | Notes |
|---|---:|---|---|
| VIN | 5V | Power | 5V gives more output power than 3.3V |
| GND | GND | Ground | Common ground with ESP32 |
| BCLK | GPIO47 | I2S bit clock | (`I2S_BCLK_PIN`) |
| LRC / WS | GPIO45 | I2S left/right clock | (`I2S_LRCLK_PIN`) |
| DIN | GPIO38 | I2S data output | (`I2S_DOUT_PIN`) — GPIO48 is RGB LED |
| SD | GPIO21 | Shutdown/mute | Optional; tie SD high on board to omit (`AMP_SD_PIN`) |
| GAIN | — | Gain select | Leave at module default (or configure per datasheet) |
| SPK+ | Speaker+ | — | Connect to speaker positive terminal |
| SPK- | Speaker- | — | Connect to speaker negative terminal |

WAV files are stored in SPIFFS. Upload them with `pio run -t uploadfs` (see `data/README.md`)

WAV playback volume can be adjusted at compile time with `CFG_WAV_MASTER_GAIN` (range `0-255`) in `platformio.ini` build flags.

## Wiring Diagram

```text
ESP32-S3 DevKitC-1                     ST7789V TFT
--------------------                   ----------------
3V3   --------------------------------> VCC
GND   --------------------------------> GND
GPIO12 (SCLK) ------------------------> SCL / SCK / CLK
GPIO11 (MOSI) ------------------------> SDA / MOSI / DIN
GPIO10 (CS)   ------------------------> CS
GPIO9  (DC)   ------------------------> DC / A0
GPIO13 (RST)  ------------------------> RST / RES
GPIO8  (BL)   ------------------------> BL / LED


ESP32-S3 DevKitC-1                     XY Joystick Module (5-pin)
--------------------                   --------------------------
GND   --------------------------------> GND
3V3   --------------------------------> +5V (VCC)
GPIO4 (ADC, X) -----------------------> VRx
GPIO5 (ADC, Y) -----------------------> VRy
GPIO7 (SW)    ------------------------> SW


ESP32-S3 DevKitC-1                     MAX98357A Amplifier (optional audio)
--------------------                   ------------------------------------
5V    --------------------------------> VIN
GND   --------------------------------> GND
GPIO47 (I2S_BCLK) --------------------> BCLK
GPIO45 (I2S_LRCLK) -------------------> LRC / WS
GPIO38 (I2S_DOUT) --------------------> DIN
GPIO21 (AMP_SD) ----------------------> SD (optional mute control)

MAX98357A SPK+ -----------------------> Speaker +
MAX98357A SPK- -----------------------> Speaker -
```

## Options
Current defaults disable controls (all `-1` in `build_flags`).

If you wire controls, set these in `platformio.ini`:

- `JOY_X_PIN` and `JOY_Y_PIN` for analog joystick axes
- `LEFT_PIN`, `RIGHT_PIN`, `FIRE_PIN` for active-low buttons

For the joystick mapping above, use:

```ini
-DJOY_X_PIN=4
-DJOY_Y_PIN=5
-DFIRE_PIN=7
```

## Build

From this folder:

```sh
pio run
```

If `pio` is not installed yet, install PlatformIO Core first:

```sh
python3 -m pip install --user platformio
```

Then add `~/.local/bin` to your `PATH` if needed.

## Upload

### 1. Upload Firmware

Flash the compiled firmware to the ESP32-S3:

```sh
~/.platformio/penv/bin/pio run -t upload
```

Hold the **BOOT** button while tapping **RESET** if the upload fails to connect.

### 2. Upload WAV Files to SPIFFS

The game sounds are stored as WAV files in the ESP32's SPIFFS filesystem partition (7 MB). After placing WAV files in the `data/` folder, upload them:

```sh
~/.platformio/penv/bin/pio run -t uploadfs
```

**Required WAV files** (see `data/README.md` for format details):
- `alert.wav` ~/.platformio/penv/bin/pio run -t uploadfs— Enemy missile spawned
- `outgoing-missile.wav` — Player missile fired
- `swoop-up.wav` — Player missile bursts
- `incoming-missile.wav` — Enemy missile destroyed
- `explode.wav` — Enemy reaches the ground line (city/battery/ground)
- `roll-up.wav` — Wave cleared
- `finale.wav` — Game over

The filesystem upload is **independent** of firmware upload — you can swap audio files without recompiling code by running only `uploadfs`.
