# Developer Notes — ESP32 Missile Command

## Build & Upload

### PlatformIO is not on PATH
The system `platformio` command is not installed globally. Always use the venv binary:
```bash
~/.platformio/penv/bin/pio run
~/.platformio/penv/bin/pio run --target upload
```
Or use the VS Code PlatformIO tasks (Build / Upload in the task runner).

### Upload port
Board enumerates as `/dev/ttyACM1` (USB CDC).

### Port busy error during upload
If you see:
```
Could not open /dev/ttyACM1: [Errno 11] Resource temporarily unavailable
```
The serial monitor is holding the port. Close the Monitor terminal first, then upload.

### Explicit upload command
```bash
~/.platformio/penv/bin/pio run --target upload --upload-port /dev/ttyACM1
```

### Serial monitor
```bash
~/.platformio/penv/bin/pio device monitor --port /dev/ttyACM1 --baud 115200
```
Boot messages are missed if the monitor connects after boot (USB CDC re-enumerates on reset).
Press the **EN/RST** button on the board while the monitor is already open to capture boot logs.

---

## Board: N16R8 Gotcha

The PlatformIO platform does not have an `esp32-s3-devkitc-1-n16r8` board ID — it will error.
The workaround in `platformio.ini` is to use the base board ID with explicit PSRAM flags:

```ini
board = esp32-s3-devkitc-1          ; base ID (N8 definition, but we override below)
board_build.arduino.memory_type = qio_opi

build_flags =
    -DBOARD_HAS_PSRAM
    -mfix-esp32-psram-cache-issue
    ...
```

This correctly enables `ps_malloc()` / PSRAM heap on the N16R8.

---

## Dual-Core Display Pipeline

The firmware uses PSRAM double-buffering to overlap game logic and SPI display output:

- **Core 1** — game logic + draw into `backbuffer` (internal SRAM, 153 KB)
- **Core 0** — SPI blit from `disp_psram` (PSRAM, 153 KB) via `display_task`

`hal_present()` on Core 1:
1. Waits for `sem_blit_done` (Core 0 finished previous blit)
2. `memcpy` backbuffer → disp_psram
3. Signals `sem_frame_ready` (Core 0 starts next blit)

If `ps_malloc()` fails at boot, the code falls back to single-core automatically.
Serial will print `[HAL] dual-core+PSRAM OK` or `[HAL] ps_malloc failed — single-buf fallback`.

---

## WebAssembly Build

```bash
cd wasm/wasm_host
./serve.sh          # builds + starts nginx
```

Config for the browser build is in `wasm/wasm_host/config.h` (student-tunable).
Config for the ESP32 build is in `src/game_config.c` (CFG_* macros).

---

## Planned: Audio (MAX98357A)
See `docs/audio-plan.md`.
