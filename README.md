# Missile Command on ESP32-S3 + ST7789V

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

(optional)
NC    --------------------------------> MISO / SDO


ESP32-S3 DevKitC-1                     XY Joystick Module (5-pin)
--------------------                   --------------------------
GND   --------------------------------> GND
3V3   --------------------------------> +5V (VCC)
GPIO4 (ADC, X) -----------------------> VRx
GPIO5 (ADC, Y) -----------------------> VRy
GPIO7 (SW)    ------------------------> SW
```

## Optional Input Wiring

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
