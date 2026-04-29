# Audio Implementation Plan — MAX98357A + MP3-to-header files

## Interim Option: Passive Piezo Buzzer
- Before the MAX98357A arrives, the ESP32 build can use a **passive piezo** for simple tone effects.
- Current stopgap implementation path:
  - `PIEZO_PIN` build flag in `platformio.ini` (default `GPIO 17`)
  - `hal_play_sound()` maps game sound IDs to short tone sequences
  - `scripts/midi_to_piezo.py` can convert a `.mid` file into a generated piezo header
  - LEDC PWM generates the square wave; no MP3 decode or I2S required
- Limits:
  - good for beeps, alarms, descending explosions, and simple musical cues
  - not suitable for real explosion samples or speech/music beds

This piezo path is intentionally separate from the future MAX98357A / MP3 path, so it can be removed cleanly later.

Current example:
- `wasm/sounds/explode.mid` is converted into `src/explode_midi_piezo.h`
- The ESP32 `SND_IMPACT` effect now plays that generated sequence

## Hardware
- **Amplifier board**: MAX98357A (I2S, mono, 3 W class D)
- **Connection**: I2S peripheral on ESP32-S3 (3 wires: BCLK, LRC/WS, DIN)
- **Speaker**: 4 Ω or 8 Ω, connected to the MAX98357A output

## Audio Data Format
- Source audio files converted to C header arrays using:
  ```
  xxd -i explosion.mp3 > explosion_data.h
  ```
- Each header exposes: `unsigned char explosion_mp3[]` and `unsigned int explosion_mp3_len`
- Files live in `src/` alongside other headers (background-1.h etc.)

## Decoding
- Use the **ESP8266Audio** library (works on ESP32-S3):
  ```ini
  lib_deps =
      earlephilhower/ESP8266Audio @ ^1.9.7
  ```
- `AudioGeneratorMP3` decodes the in-memory array
- `AudioFileSourcePROGMEM` wraps the header array as an audio source
- `AudioOutputI2S` sends decoded PCM to the MAX98357A

## Core Assignment
- Audio task runs on **Core 0**, priority **2** (higher than display_task at priority 1)
- This allows audio to preempt the display task during the ~5 ms idle gap between SPI blits
- The I2S DMA buffer should be sized to **≥ 16 KB** to bridge the ~25 ms SPI blit window without underruns
  - At 44.1 kHz / 16-bit mono: ~22 KB/s → 16 KB ≈ 90 ms headroom — sufficient

## Task Structure
```cpp
// In hal_esp32.cpp or a new hal_audio.cpp
static void audio_task(void *) {
    // init I2S output + MP3 generator here
    for (;;) {
        if (mp3->isRunning()) {
            if (!mp3->loop()) mp3->stop();
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
}

// In hal_init(), after display task:
xTaskCreatePinnedToCore(audio_task, "audio", 8192, nullptr, 2, nullptr, 0);
```

## hal.h Integration
- Add `hal_play_sound(int sound_id)` implementation (currently a no-op stub in hal_esp32.cpp)
- Sound IDs are already defined in `missile_command.h` / `game_config.h`
- `hal_play_sound` enqueues a sound ID via a FreeRTOS queue; `audio_task` dequeues and starts playback

## Queue Design
```cpp
static QueueHandle_t sound_queue;  // created in hal_init()

void hal_play_sound(int sound_id) {
    xQueueSend(sound_queue, &sound_id, 0);  // non-blocking; drop if full
}
```

## Sound Assets Needed
| Sound ID | File | Notes |
|----------|------|-------|
| SOUND_EXPLOSION | explosion.mp3 | missile/city hit |
| SOUND_LAUNCH | launch.mp3 | player fires |
| SOUND_LEVEL_UP | level_up.mp3 | wave complete |
| SOUND_GAME_OVER | game_over.mp3 | all cities lost |

## PSRAM Note
- Audio decode buffers (~20 KB) can also be allocated from PSRAM if internal SRAM becomes tight
- Use `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)` for those buffers

## Wiring (suggested GPIO assignments)
| Signal | GPIO | Notes |
|--------|------|-------|
| I2S BCLK | 14 | avoid ADC pins |
| I2S WS/LRC | 15 | |
| I2S DIN | 16 | |

GPIO 14–16 are free on the current board config. Confirm no conflict with SPI (11/12/13) or joystick (4/5).

## Steps to Implement
1. Wire MAX98357A to ESP32-S3
2. Convert audio files: `xxd -i file.mp3 > file_data.h`
3. Add ESP8266Audio to `lib_deps` in platformio.ini
4. Create `src/hal_audio.cpp` with `audio_task` + queue
5. Implement `hal_play_sound()` in hal_esp32.cpp (replace the no-op stub)
6. Test with a single sound before adding all assets
