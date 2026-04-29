# ESP32-S3 Hardware Architecture — Missile Command

## Board
- ESP32-S3-DevKitC-1 **N16R8** (16 MB flash, 8 MB OPI PSRAM)
- Dual-core Xtensa LX7 @ 240 MHz

## Display
- ST7789V SPI TFT, 240×320 native, rotated to 320×240 landscape
- SPI @ 40 MHz; `tft.drawRGBBitmap()` blocks for ~25 ms per full frame
- Pins: CS=10, DC=9, RST=13, MOSI=11, SCLK=12, BL=8

## Framebuffer / Double-buffer
- `GFXcanvas16* backbuffer` (153 KB) allocated from internal SRAM — draw target for Core 1
- `uint16_t* disp_psram` (153 KB) allocated from PSRAM via `ps_malloc()` — blit source for Core 0
- `board_build.arduino.memory_type = qio_opi` and `-DBOARD_HAS_PSRAM` required in platformio.ini

## Core Assignment
| Core | Task | Priority | Notes |
|------|------|----------|-------|
| Core 1 | Arduino loop() — game logic + draw | default | writes to backbuffer |
| Core 0 | display_task — SPI blit | 1 | reads from disp_psram |
| Core 0 | audio_task (planned) | 2 | preempts display_task in idle gaps |

## Synchronization (display pipeline)
- `sem_blit_done`  — Core 0 → Core 1: blit finished, safe to memcpy new frame
- `sem_frame_ready` — Core 1 → Core 0: new frame copied to PSRAM, start blit
- Both are FreeRTOS binary semaphores

## Frame Rate
- Old (single-core): ~33 fps (30 ms cap in main.cpp)
- New (dual-core): ceiling ~40 fps; main.cpp cap loosened to 16 ms (~60 fps ceiling, SPI sets real rate)
- `FRAME_MS` still defined in missile_command.h as `1000/TARGET_FPS` (30) but not used as hard cap anymore

## Input
- Joystick: ADC pins 4 (X), 5 (Y); absolute mode; smoothing 31/32
- Buttons: LEFT_PIN, RIGHT_PIN, FIRE_PIN (all configurable via build_flags)

## Planned: Audio (MAX98357A)
- See docs/audio-plan.md for full implementation plan
