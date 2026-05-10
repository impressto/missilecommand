# Missile Command — Game Sound WAV Files

Place WAV files in this folder, then upload them to the ESP32 SPIFFS partition:

```bash
~/.platformio/penv/bin/pio run -t uploadfs
```

This uploads only the `data/` folder without recompiling firmware.

## Required Files

| Filename | Sound event | Sound ID |
|---|---|---|
| `launch.wav` | Player missile fired | `SND_LAUNCH` |
| `player_burst.wav` | Player missile reaches target | `SND_PLAYER_BURST` |
| `intercept.wav` | Enemy missile destroyed | `SND_INTERCEPT` |
| `impact.wav` | Enemy missile hits ground/city | `SND_IMPACT` |
| `alert.wav` | Enemy missile spawned | `SND_ALERT` |
| `wave_complete.wav` | Wave clears, next wave starts | `SND_WAVE_COMPLETE` |
| `game_over.wav` | All cities destroyed | `SND_GAME_OVER` |

Files that are missing simply produce a serial warning and no audio — they do not crash the game.

## WAV Format Requirements

Match the format from the test project:

- **Format**: PCM (uncompressed), not MP3/AAC/etc.
- **Bit depth**: 16-bit
- **Channels**: Mono or stereo (mono is automatically expanded to stereo by the driver)
- **Sample rate**: 44.1 kHz recommended (other rates will play at correct pitch — the I2S clock is reconfigured per file)
- **File size**: Keep individual effects under ~500 KB; the SPIFFS partition holds ~1.5 MB total

## Converting with ffmpeg

```bash
# From any audio format → correct WAV format
ffmpeg -i input.mp3 -acodec pcm_s16le -ar 44100 -ac 2 launch.wav
```

## Source: MIDI Files

The original MIDI sequences used by the old piezo buzzer are in `wasm/sounds/`.
You can render them to WAV with a soundfont and a MIDI renderer, for example:

```bash
fluidsynth -ni soundfont.sf2 wasm/sounds/explode.mid -F impact.wav -r 44100
```

Then convert with ffmpeg to ensure the right format.
