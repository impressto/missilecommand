# Missile Command on ESP32-S3

<img width="1600" height="1139" alt="overview" src="https://github.com/user-attachments/assets/724ff416-901e-45b8-a31b-06c41ccd0f87" />

This project is a classroom-friendly version of Missile Command for an ESP32-S3 microcontroller. Students can use it to practice wiring, reading inputs, sending graphics to a display, and optionally playing sound.

The default settings in this folder match the pin choices in `platformio.ini` and `src/hal_esp32.cpp`.

## What Students Will Learn

- How a microcontroller talks to a screen over SPI
- How a joystick or buttons become game controls
- How a game can store images and sound files separately from the code
- How to build firmware and upload it to a real device

## Parts Used

- ESP32-S3 DevKitC-1
- ST7789V SPI TFT display, 240 x 320
- Optional: dual-axis joystick
- Optional: MAX98357A I2S audio amplifier and speaker

## How The Game Fits Together

The game runs in landscape mode, so the display is rotated to fit a 320 x 240 play area.

The ESP32 version uses image files that have been converted into C headers inside `src/`:

- `background-1.h`
- `civilian-target.h`
- `bunker-1.h`

These are RGB565 images. Black pixels are treated as transparent, so they do not get drawn.

## Wiring Guide

If you are building this in class, wire the display first, then add controls, then add audio if your teacher wants the extension activity.

### Display

| ST7789V Pin | ESP32-S3 Pin | Notes |
|---|---:|---|
| VCC | 3V3 | Use a 3.3V display module |
| GND | GND | Common ground |
| SCL / SCK / CLK | GPIO12 | SPI clock |
| SDA / MOSI / DIN | GPIO11 | SPI data |
| CS | GPIO10 | Chip select |
| DC / A0 | GPIO9 | Data or command |
| RST / RES | GPIO13 | Reset |
| BL / LED | GPIO8 | Backlight |
| MISO / SDO | Not connected | Not used |

### Joystick

A common joystick module has pins labeled GND, +5V, VRx, VRy, and SW.

| Joystick Pin | ESP32-S3 Pin | Notes |
|---|---:|---|
| GND | GND | Common ground |
| +5V | 3V3 | Power from the ESP32-S3 |
| VRx | GPIO4 | Horizontal movement |
| VRy | GPIO5 | Vertical movement |
| SW | GPIO7 | Optional fire button |

### Optional Audio

The MAX98357A board can play the game sounds through a speaker.

| MAX98357A Pin | ESP32-S3 Pin | Notes |
|---|---:|---|
| VIN | 5V | More speaker power than 3.3V |
| GND | GND | Common ground |
| BCLK | GPIO47 | I2S clock |
| LRC / WS | GPIO45 | I2S word select |
| DIN | GPIO38 | I2S data |
| SD | GPIO21 | Optional enable or mute |
| SPK+ | Speaker + | Speaker output |
| SPK- | Speaker - | Speaker output |

Sound files are stored in SPIFFS. Upload them separately from the firmware using `uploadfs`.

## Quick Wiring Diagram

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


ESP32-S3 DevKitC-1                     XY Joystick Module
--------------------                   -------------------
GND   --------------------------------> GND
3V3   --------------------------------> +5V
GPIO4 (ADC, X) -----------------------> VRx
GPIO5 (ADC, Y) -----------------------> VRy
GPIO7 (SW)    ------------------------> SW


ESP32-S3 DevKitC-1                     MAX98357A Amplifier (optional)
--------------------                   ------------------------------
5V    --------------------------------> VIN
GND   --------------------------------> GND
GPIO47 (I2S_BCLK) --------------------> BCLK
GPIO45 (I2S_LRCLK) -------------------> LRC / WS
GPIO38 (I2S_DOUT) --------------------> DIN
GPIO21 (AMP_SD) ----------------------> SD

MAX98357A SPK+ -----------------------> Speaker +
MAX98357A SPK- -----------------------> Speaker -
```

## Before You Build

The default configuration leaves the controls disabled until you choose the pins in `platformio.ini`.

Use these settings for the joystick shown above:

```ini
-DJOY_X_PIN=4
-DJOY_Y_PIN=5
-DFIRE_PIN=7
```

If you want left and right buttons instead of a joystick, set `LEFT_PIN`, `RIGHT_PIN`, and `FIRE_PIN`.

## Build The Project

From this folder, use the local PlatformIO binary:

```sh
~/.platformio/penv/bin/pio run
```

If you are using VS Code, the PlatformIO Build task does the same thing.

## Upload To The Board

### 1. Upload The Firmware

```sh
~/.platformio/penv/bin/pio run -t upload
```

If the upload does not connect, hold BOOT while tapping RESET on the board.

### 2. Upload Sound Files

Place WAV files in `data/`, then upload them with:

```sh
~/.platformio/penv/bin/pio run -t uploadfs
```

This only uploads the sound files. It does not rebuild the firmware.

See `data/README.md` for the exact WAV file names and format.

## Sound File Names

The game looks for these files in SPIFFS:

- `launch.wav` for firing a missile
- `player_burst.wav` for a player missile reaching a target
- `intercept.wav` for destroying an enemy missile
- `impact.wav` for an enemy missile hitting the ground, city, or bunker
- `alert.wav` for an enemy missile appearing
- `wave_complete.wav` when a wave ends
- `game_over.wav` when all cities are gone

## Teaching Notes

This project works well for a class lab because students can change one part at a time.

Good checkpoints for students:

1. Get the display to show the game screen.
2. Make the joystick move the cursor or launcher.
3. Add sound after the graphics are working.
4. Try changing values in `src/game_config.c` to see how the game feels.

If you want the classroom build to stay simple, start with display plus controls first, then treat audio as an extension activity.
