# Missile Command — WebAssembly → ESP32

A faithful recreation of the 1980 Atari classic, built so that the **same game
logic compiles unchanged** on both a web browser (via WebAssembly/Emscripten)
and an ESP32 microcontroller (via PlatformIO).

---

## Project structure

```
webassembly/
├── game/                     ← Shared game logic (pure C, no platform code)
│   ├── hal.h                 ← Hardware Abstraction Layer — the only interface
│   ├── missile_command.h     ← Game public API
│   └── missile_command.c     ← Game logic (update + render via HAL calls)
│
├── wasm_host/                ← WebAssembly / browser target
│   ├── hal_wasm.c            ← HAL implementation for Emscripten
│   ├── main_wasm.c           ← Browser entry point (requestAnimationFrame)
│   ├── shell.html            ← HTML page that hosts the canvas
│   └── Makefile              ← Build with `make` or `make serve`
│
└── esp32/                    ← ESP32 / PlatformIO target
    ├── platformio.ini        ← Board, libraries, source paths
    └── src/
        ├── hal_esp32.cpp     ← HAL implementation for ILI9341 + joystick
        └── main.cpp          ← Arduino setup()/loop() entry point
```

---

## How portability works

All drawing and input goes through **`hal.h`** functions like `hal_draw_pixel()`,
`hal_draw_circle()`, and `hal_read_input()`.

| Platform   | HAL file         | Entry point      |
|-----------|-----------------|-----------------|
| Browser    | `hal_wasm.c`    | `main_wasm.c`   |
| ESP32      | `hal_esp32.cpp` | `src/main.cpp`  |

`missile_command.c` never includes `<Arduino.h>`, `<emscripten.h>`, or any
display driver — only `hal.h` and standard C headers (`stdint.h`, `string.h`).

---

## Building — WebAssembly

### Prerequisites
- [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)

```bash
# Activate Emscripten (once per terminal session)
cd emsdk

./emsdk_env.sh

cd ../wasm_host

# Build
make

# Build + open in browser via local server
make serve
# → http://localhost:8080/missile_command.html
```

### Controls (browser)
| Action | Control |
|--------|---------|
| Aim    | Mouse   |
| Fire   | Left click or Space / Z |

---

## Building — ESP32

### Prerequisites
- [VS Code + PlatformIO extension](https://platformio.org/install/ide?install=vscode)
- ILI9341 TFT display wired as described in `esp32/src/hal_esp32.cpp`
- Analog joystick (X/Y on ADC pins) + one push-button

```bash
cd esp32
pio run --target upload
```

### Wiring (defaults — change pins in `hal_esp32.cpp`)
| Signal    | ESP32 GPIO |
|-----------|-----------|
| TFT CS    | 5         |
| TFT DC    | 2         |
| TFT RST   | 4         |
| TFT MOSI  | 23        |
| TFT CLK   | 18        |
| Joystick X | 34       |
| Joystick Y | 35       |
| Fire button | 12      |

---

## Porting to a new platform

1. Copy `hal_esp32.cpp` as a starting template.
2. Implement every function declared in `game/hal.h`.
3. Write a `main` / entry point that calls `hal_init()`, `game_init()`, then
   loops calling `game_update(delta_ms)` and `game_render()`.
4. Do **not** modify `missile_command.c` or `hal.h`.

---

## Game overview

- Enemy missiles rain down from the top of the screen toward your cities.
- Aim the crosshair and fire interceptor missiles from your batteries.
- Interceptors explode at the target point; anything inside the blast radius
  is destroyed.
- Protect at least one city to survive each wave.
- Waves get faster; batteries restock between waves.

---

## Extending the game (ideas for students)

- Add a high-score table saved to ESP32 flash (NVS/LittleFS).
- Add sound effects via the ESP32 DAC or a piezo buzzer.
- Add MIRVs (missiles that split mid-flight).
- Add a title/attract screen.
- Implement a restart flow after game over.
- Port the HAL to a different display (ST7735, OLED, etc.).
