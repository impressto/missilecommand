/**
 * hal_esp32.cpp — ESP32-S3 (Arduino/PlatformIO) implementation of hal.h
 *
 * Default wiring targets an ESP32-S3 DevKitC-1 paired with an ST7789V SPI
 * display. All pins are compile-time overrides from platformio.ini so the
 * same source can be reused with different breakout boards.
 */

#include "hal.h"

#include <Arduino.h>
#include <SPI.h>
#include <pgmspace.h>
#include <string.h>
#include <limits.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <driver/i2s.h>
#include <SPIFFS.h>

#include "game_config.h"
#include "background-1.h"
#include "civilian-target.h"
#include "bunker-1.h"
#include "explode_midi_piezo.h"
#include "missile2_midi_piezo.h"
#include "swoopup_midi_piezo.h"
#include "alert_midi_piezo.h"
#include "rollup_midi_piezo.h"

#ifndef TFT_CS
#define TFT_CS   10
#endif
#ifndef TFT_DC
#define TFT_DC   9
#endif
#ifndef TFT_RST
#define TFT_RST  13
#endif
#ifndef TFT_MOSI
#define TFT_MOSI 11
#endif
#ifndef TFT_SCLK
#define TFT_SCLK 12
#endif
#ifndef TFT_MISO
#define TFT_MISO -1
#endif
#ifndef TFT_BL
#define TFT_BL   8
#endif

#ifndef JOY_X_PIN
#define JOY_X_PIN   -1
#endif
#ifndef JOY_Y_PIN
#define JOY_Y_PIN   -1
#endif
#ifndef LEFT_PIN
#define LEFT_PIN    -1
#endif
#ifndef RIGHT_PIN
#define RIGHT_PIN   -1
#endif
#ifndef FIRE_PIN
#define FIRE_PIN    -1
#endif
#ifndef FIRE_PIN_2
#define FIRE_PIN_2  -1
#endif
#ifndef PIEZO_PIN
#define PIEZO_PIN   -1
#endif
#ifndef PIEZO_LEDC_CHANNEL
#define PIEZO_LEDC_CHANNEL 7
#endif

/* ── I2S / MAX98357A pin defaults ────────────────────────────────────────── */
/* Override in platformio.ini build_flags.  Set DOUT to a valid GPIO to      */
/* activate I2S audio; leave at -1 to fall back to the piezo buzzer.        */
#ifndef I2S_BCLK_PIN
#define I2S_BCLK_PIN  -1
#endif
#ifndef I2S_LRCLK_PIN
#define I2S_LRCLK_PIN -1
#endif
#ifndef I2S_DOUT_PIN
#define I2S_DOUT_PIN  -1
#endif
/* AMP_SD controls the MAX98357A SD (shutdown) pin.  Optional: tie SD high   */
/* on the board if you don't need software mute, and leave this as -1.      */
#ifndef AMP_SD_PIN
#define AMP_SD_PIN    -1
#endif

/* Per-sound enable flags — set to 0 in platformio.ini build_flags to silence. */
#ifndef CFG_SND_LAUNCH_EN
#define CFG_SND_LAUNCH_EN       0
#endif
#ifndef CFG_SND_PLAYER_BURST_EN
#define CFG_SND_PLAYER_BURST_EN 1
#endif
#ifndef CFG_SND_INTERCEPT_EN
#define CFG_SND_INTERCEPT_EN    1
#endif
#ifndef CFG_SND_IMPACT_EN
#define CFG_SND_IMPACT_EN       1
#endif
#ifndef CFG_SND_ALERT_EN
#define CFG_SND_ALERT_EN        0
#endif
#ifndef CFG_SND_WAVE_COMPLETE_EN
#define CFG_SND_WAVE_COMPLETE_EN 1
#endif
#ifndef CFG_SND_GAME_OVER_EN
#define CFG_SND_GAME_OVER_EN    1
#endif

#ifndef TFT_WIDTH
#define TFT_WIDTH   240
#endif
#ifndef TFT_HEIGHT
#define TFT_HEIGHT  320
#endif
#ifndef TFT_ROTATION
#define TFT_ROTATION 1
#endif
#ifndef TFT_SPI_HZ
#define TFT_SPI_HZ 40000000
#endif

/* ── ADC centre/dead-zone calibration ────────────────────────────────────── */
#define ADC_MAX     4095
#define ADC_MID     (ADC_MAX / 2)
#define JOY_DEAD    200   /* dead-zone counts around centre */
#define JOY_STEP_DIVISOR 200
#define JOY_STEP_MAX 3
#define BUTTON_CURSOR_STEP 4
#ifndef JOY_ABSOLUTE_MODE
#define JOY_ABSOLUTE_MODE 0
#endif
#define JOY_INVERT_X 0
#define JOY_INVERT_Y 0

#ifndef JOY_ABS_DEAD_RAW
#define JOY_ABS_DEAD_RAW 260
#endif

#ifndef JOY_CAL_X_MIN
#define JOY_CAL_X_MIN 0
#endif
#ifndef JOY_CAL_X_MAX
#define JOY_CAL_X_MAX ADC_MAX
#endif
#ifndef JOY_CAL_Y_MIN
#define JOY_CAL_Y_MIN 0
#endif
#ifndef JOY_CAL_Y_MAX
#define JOY_CAL_Y_MAX ADC_MAX
#endif

#ifndef JOY_ABS_SMOOTH_NUM
#define JOY_ABS_SMOOTH_NUM 7
#endif

#ifndef JOY_ABS_SMOOTH_DEN
#define JOY_ABS_SMOOTH_DEN 8
#endif

/* ── Utility functions ───────────────────────────────────────────────────── */
static int isqrt_esp32(int n) {
    if (n <= 0) return 0;
    int x = n, y = 1;
    while (x > y) { x = (x + y) / 2; y = n / x; }
    return x;
}

/* ── Display driver instance ─────────────────────────────────────────────── */
static Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);

/* ── Double-buffer via PSRAM ─────────────────────────────────────────────── */
/* Core 1 draws into backbuffer (GFXcanvas16, internal SRAM — same as       */
/* before). On hal_present() it memcpy's to disp_psram (PSRAM, ~153 KB) and  */
/* signals Core 0 to blit over SPI.  The two tasks fully overlap.           */
static GFXcanvas16    *backbuffer       = nullptr;   /* draw target, Core 1  */
static uint16_t       *disp_psram       = nullptr;   /* blit source, Core 0  */
static SemaphoreHandle_t sem_frame_ready = nullptr;  /* Core 1 → Core 0      */
static SemaphoreHandle_t sem_blit_done   = nullptr;  /* Core 0 → Core 1      */
static bool use_backbuffer = false;
static bool use_dual_core  = false;
static QueueHandle_t sound_queue = nullptr;
static bool use_piezo = false;

/* ── I2S / MAX98357A audio state ─────────────────────────────────────────── */
static const i2s_port_t   kI2SPort          = I2S_NUM_0;
static const uint32_t     kI2SSampleRate     = 44100;
static const size_t       kI2SFramesPerChunk = 256;
static QueueHandle_t      i2s_sound_queue    = nullptr;
static bool               use_i2s_audio      = false;

static void piezo_silence(void) {
    if (!use_piezo) {
        return;
    }
    ledcWriteTone(PIEZO_LEDC_CHANNEL, 0);
    ledcWrite(PIEZO_LEDC_CHANNEL, 0);
}

static void piezo_step(uint16_t freq_hz, uint16_t duration_ms) {
    if (!use_piezo || duration_ms == 0) {
        return;
    }
    if (freq_hz == 0) {
        piezo_silence();
    } else {
        ledcWriteTone(PIEZO_LEDC_CHANNEL, freq_hz);
        ledcWrite(PIEZO_LEDC_CHANNEL, 127);
    }
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
}

static void piezo_play_sequence(const uint16_t steps[][2], int step_count) {
    for (int i = 0; i < step_count; i++) {
        piezo_step(steps[i][0], steps[i][1]);
    }
}

static void piezo_play_pattern(int sound_id) {
    switch (sound_id) {
#if CFG_SND_LAUNCH_EN
        case SND_LAUNCH:
            piezo_play_sequence(missile2_midi_piezo, missile2_midi_piezo_count);
            break;
#endif
#if CFG_SND_PLAYER_BURST_EN
        case SND_PLAYER_BURST:
            piezo_play_sequence(swoopup_midi_piezo, swoopup_midi_piezo_count);
            break;
#endif
#if CFG_SND_INTERCEPT_EN
        case SND_INTERCEPT:
            piezo_step(1760, 25);
            piezo_step(1480, 25);
            piezo_step(1244, 40);
            break;
#endif
#if CFG_SND_IMPACT_EN
        case SND_IMPACT:
            piezo_play_sequence(explode_midi_piezo, explode_midi_piezo_count);
            break;
#endif
#if CFG_SND_ALERT_EN
        case SND_ALERT:
            piezo_play_sequence(alert_midi_piezo, alert_midi_piezo_count);
            break;
#endif
#if CFG_SND_WAVE_COMPLETE_EN
        case SND_WAVE_COMPLETE:
            piezo_play_sequence(rollup_midi_piezo, rollup_midi_piezo_count);
            break;
#endif
#if CFG_SND_GAME_OVER_EN
        case SND_GAME_OVER:
            piezo_step(523, 100);
            piezo_step(392, 130);
            piezo_step(262, 180);
            break;
#endif
        default:
            break;
    }
    piezo_silence();
}

static void piezo_task(void *) {
    int sound_id = 0;
    for (;;) {
        if (xQueueReceive(sound_queue, &sound_id, portMAX_DELAY) == pdTRUE) {
            piezo_play_pattern(sound_id);
        }
    }
}

/* ── I2S / MAX98357A WAV player ──────────────────────────────────────────── */

static const char* i2s_sound_filename(int sound_id) {
    switch (sound_id) {
        case SND_LAUNCH:        return "/missile-2.wav";
        case SND_PLAYER_BURST:  return "/swoop-up.wav";
        case SND_INTERCEPT:     return nullptr;  /* Disabled */
        case SND_IMPACT:        return "/explode.wav";
        case SND_ALERT:         return nullptr;  /* Disabled */
        case SND_WAVE_COMPLETE: return "/roll-up.wav";
        case SND_GAME_OVER:     return "/finale.wav";
        default:                return nullptr;
    }
}

struct I2SWavHeader {
    uint16_t num_channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint32_t data_size;
};

static bool i2s_parse_wav_header(File& file, I2SWavHeader& hdr) {
    file.seek(0);
    char tag[4];
    uint32_t tmp;

    /* RIFF tag */
    if (file.read((uint8_t*)tag, 4) != 4) return false;
    if (tag[0]!='R' || tag[1]!='I' || tag[2]!='F' || tag[3]!='F') return false;
    file.read((uint8_t*)&tmp, 4); /* RIFF chunk size */

    /* WAVE tag */
    if (file.read((uint8_t*)tag, 4) != 4) return false;
    if (tag[0]!='W' || tag[1]!='A' || tag[2]!='V' || tag[3]!='E') return false;

    /* Scan for fmt chunk */
    bool found_fmt = false;
    while (!found_fmt) {
        if (file.read((uint8_t*)tag, 4) != 4) return false;
        uint32_t chunk_size;
        if (file.read((uint8_t*)&chunk_size, 4) != 4) return false;
        if (tag[0]=='f' && tag[1]=='m' && tag[2]=='t' && tag[3]==' ') {
            uint8_t fmt[16];
            uint32_t to_read = (chunk_size < 16u) ? chunk_size : 16u;
            if (file.read(fmt, to_read) != (int)to_read) return false;
            if (chunk_size > 16u) file.seek(file.position() + chunk_size - 16u);
            hdr.num_channels    = *(uint16_t*)(fmt + 2);
            hdr.sample_rate     = *(uint32_t*)(fmt + 4);
            hdr.bits_per_sample = *(uint16_t*)(fmt + 14);
            found_fmt = true;
        } else {
            file.seek(file.position() + chunk_size);
        }
    }

    /* Scan for data chunk */
    for (;;) {
        if (file.read((uint8_t*)tag, 4) != 4) return false;
        uint32_t chunk_size;
        if (file.read((uint8_t*)&chunk_size, 4) != 4) return false;
        if (tag[0]=='d' && tag[1]=='a' && tag[2]=='t' && tag[3]=='a') {
            hdr.data_size = chunk_size;
            return true;
        }
        file.seek(file.position() + chunk_size);
    }
}

static void i2s_stream_wav(File& file, const I2SWavHeader& hdr) {
    /* Reconfigure I2S clock/channels for this file if needed */
    const i2s_channel_t ch = (hdr.num_channels == 1) ? I2S_CHANNEL_MONO : I2S_CHANNEL_STEREO;
    if (hdr.sample_rate != kI2SSampleRate || hdr.num_channels != 2) {
        i2s_set_clk(kI2SPort, hdr.sample_rate, I2S_BITS_PER_SAMPLE_16BIT, ch);
    }

    /* Buffer: sized for one chunk of stereo 16-bit frames */
    static int16_t buf[kI2SFramesPerChunk * 2];
    uint32_t remaining = hdr.data_size;

    while (remaining > 0) {
        /* Check if a new sound has arrived — interrupt current playback */
        int dummy;
        if (xQueuePeek(i2s_sound_queue, &dummy, 0) == pdTRUE) {
            break; /* New sound waiting, stop current playback */
        }

        /* For mono we only fill half the buffer so we have room to expand */
        const size_t max_read = (hdr.num_channels == 1)
                                ? sizeof(buf) / 2
                                : sizeof(buf);
        const size_t to_read  = (remaining < (uint32_t)max_read)
                                ? (size_t)remaining
                                : max_read;
        const int n_read = file.read((uint8_t*)buf, to_read);
        if (n_read <= 0) break;
        remaining -= (uint32_t)n_read;

        size_t written;
        if (hdr.num_channels == 1) {
            /* Expand mono → stereo in-place (iterate backward to avoid clobber) */
            const int n_samples = n_read / 2;
            for (int i = n_samples - 1; i >= 0; i--) {
                buf[i * 2 + 1] = buf[i];
                buf[i * 2]     = buf[i];
            }
            i2s_write(kI2SPort, buf, (size_t)(n_read * 2), &written, portMAX_DELAY);
        } else {
            i2s_write(kI2SPort, buf, (size_t)n_read, &written, portMAX_DELAY);
        }
    }

    /* Restore default sample rate if we changed it */
    if (hdr.sample_rate != kI2SSampleRate) {
        i2s_set_clk(kI2SPort, kI2SSampleRate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
    }
}

static void i2s_audio_task(void*) {
    int sound_id;
    for (;;) {
        if (xQueueReceive(i2s_sound_queue, &sound_id, portMAX_DELAY) != pdTRUE) continue;
        
        /* Flush any additional queued sounds to play only the most recent */
        int newer_sound;
        while (xQueueReceive(i2s_sound_queue, &newer_sound, 0) == pdTRUE) {
            sound_id = newer_sound;
        }
        
        const char* fname = i2s_sound_filename(sound_id);
        if (!fname) continue;
        File f = SPIFFS.open(fname, FILE_READ);
        if (!f) {
            Serial.printf("[I2S] WAV not found: %s\n", fname);
            continue;
        }
        I2SWavHeader hdr;
        if (!i2s_parse_wav_header(f, hdr)) {
            Serial.printf("[I2S] Bad WAV header: %s\n", fname);
            f.close();
            continue;
        }
        i2s_stream_wav(f, hdr);
        f.close();
    }
}

static void display_task(void *) {
    for (;;) {
        xSemaphoreTake(sem_frame_ready, portMAX_DELAY);
        tft.drawRGBBitmap(0, 0, disp_psram, SCREEN_W, SCREEN_H);
        xSemaphoreGive(sem_blit_done);
    }
}

/* ── Image assets exported as RGB565 arrays in PROGMEM ───────────────────── */
static constexpr uint16_t TRANSPARENT_KEY = 0x0000;
static constexpr int BG_W = SCREEN_W;
static constexpr int BG_H = SCREEN_H;
static constexpr int CITY_SPRITE_W = 20;
static constexpr int CITY_SPRITE_H = 20;
static constexpr int BUNKER_SPRITE_W = 32;
static constexpr int BUNKER_SPRITE_H = 20;
static constexpr int CITY_SPRITE_Y_OFFSET = 0;
static constexpr int BUNKER_SPRITE_Y_OFFSET = 0;

static_assert((int)(sizeof(background_1) / sizeof(background_1[0])) == BG_W * BG_H,
              "background-1.h dimensions must be 320x240");
static_assert((int)(sizeof(civilian_target) / sizeof(civilian_target[0])) == CITY_SPRITE_W * CITY_SPRITE_H,
              "civilian-target.h dimensions must match CITY_SPRITE_W/H");
static_assert((int)(sizeof(bunker_1) / sizeof(bunker_1[0])) == BUNKER_SPRITE_W * BUNKER_SPRITE_H,
              "bunker-1.h dimensions must match BUNKER_SPRITE_W/H");

/* ── Timing ──────────────────────────────────────────────────────────────── */
static uint32_t start_ms = 0;
static int cursor_x = SCREEN_W / 2;
static int cursor_y = SCREEN_H / 2;
static int joy_center_x = ADC_MID;
static int joy_center_y = ADC_MID;
static int joy_filtered_x = ADC_MID;
static int joy_filtered_y = ADC_MID;
static uint32_t last_cursor_sample_ms = UINT32_MAX;

static int read_axis_raw_averaged(int pin) {
    if (pin < 0) {
        return ADC_MID;
    }
    long sum = 0;
    const int samples = 8;
    for (int i = 0; i < samples; i++) {
        sum += analogRead(pin);
    }
    return (int)(sum / samples);
}

static int map_axis_to_coord(int raw, int center,
                             int min_raw, int max_raw,
                             int upper_bound, int invert) {
    if (raw < 0) raw = 0;
    if (raw > ADC_MAX) raw = ADC_MAX;
    if (upper_bound <= 1) {
        return 0;
    }

    if (min_raw > max_raw) {
        int t = min_raw;
        min_raw = max_raw;
        max_raw = t;
    }
    if (min_raw < 0) min_raw = 0;
    if (max_raw > ADC_MAX) max_raw = ADC_MAX;

    if ((max_raw - min_raw) < 64) {
        min_raw = 0;
        max_raw = ADC_MAX;
    }

    if (raw < min_raw) raw = min_raw;
    if (raw > max_raw) raw = max_raw;

    const int range = max_raw - min_raw;
    int coord = ((raw - min_raw) * (upper_bound - 1) + (range / 2)) / range;

    const int delta = raw - center;
    int abs_delta = (delta < 0) ? -delta : delta;

    if (abs_delta <= JOY_ABS_DEAD_RAW) {
        return (upper_bound - 1) / 2;
    }

    if (invert) {
        coord = (upper_bound - 1) - coord;
    }
    if (coord < 0) coord = 0;
    if (coord >= upper_bound) coord = upper_bound - 1;
    return coord;
}

static int apply_dead_zone(int raw, int center) {
    int diff = raw - center;
    if (diff > -JOY_DEAD && diff < JOY_DEAD) {
        return 0;
    }
    return diff;
}

static void calibrate_axis_center(int pin, int *center_out) {
    if (pin < 0) {
        return;
    }
    long sum = 0;
    const int samples = 32;
    for (int i = 0; i < samples; i++) {
        sum += read_axis_raw_averaged(pin);
        delay(2);
    }
    *center_out = (int)(sum / samples);
}

static void configure_analog_input_pin(int pin) {
    if (pin < 0) {
        return;
    }
    pinMode(pin, INPUT);
    analogSetPinAttenuation(pin, ADC_11db);
}

static void configure_input_pin(int pin) {
    if (pin >= 0) {
        pinMode(pin, INPUT_PULLUP);
    }
}

static bool button_pressed(int pin) {
    return pin >= 0 && digitalRead(pin) == LOW;
}

static void update_cursor_axis(int pin, int *value, int upper_bound) {
    if (pin < 0) {
        return;
    }

    int center = (pin == JOY_X_PIN) ? joy_center_x : joy_center_y;
    int diff = apply_dead_zone(read_axis_raw_averaged(pin), center);
    int step = diff / JOY_STEP_DIVISOR;
    if (step > JOY_STEP_MAX) step = JOY_STEP_MAX;
    if (step < -JOY_STEP_MAX) step = -JOY_STEP_MAX;
    *value += step;
    if (*value < 0) {
        *value = 0;
    }
    if (*value >= upper_bound) {
        *value = upper_bound - 1;
    }
}

static void sample_cursor_once(void) {
    const uint32_t now = millis();
    if (now == last_cursor_sample_ms) {
        return;
    }
    last_cursor_sample_ms = now;

    if (JOY_ABSOLUTE_MODE && JOY_X_PIN >= 0 && JOY_Y_PIN >= 0) {
        int raw_x = read_axis_raw_averaged(JOY_X_PIN);
        int raw_y = read_axis_raw_averaged(JOY_Y_PIN);

        joy_filtered_x = (JOY_ABS_SMOOTH_NUM * joy_filtered_x + ((JOY_ABS_SMOOTH_DEN - JOY_ABS_SMOOTH_NUM) * raw_x)) / JOY_ABS_SMOOTH_DEN;
        joy_filtered_y = (JOY_ABS_SMOOTH_NUM * joy_filtered_y + ((JOY_ABS_SMOOTH_DEN - JOY_ABS_SMOOTH_NUM) * raw_y)) / JOY_ABS_SMOOTH_DEN;

        cursor_x = map_axis_to_coord(joy_filtered_x, joy_center_x,
                         JOY_CAL_X_MIN, JOY_CAL_X_MAX,
                         SCREEN_W, JOY_INVERT_X);
        cursor_y = map_axis_to_coord(joy_filtered_y, joy_center_y,
                         JOY_CAL_Y_MIN, JOY_CAL_Y_MAX,
                         SCREEN_H, JOY_INVERT_Y);
    } else {
        update_cursor_axis(JOY_X_PIN, &cursor_x, SCREEN_W);
        update_cursor_axis(JOY_Y_PIN, &cursor_y, SCREEN_H);
    }

    if (button_pressed(LEFT_PIN))  cursor_x -= BUTTON_CURSOR_STEP;
    if (button_pressed(RIGHT_PIN)) cursor_x += BUTTON_CURSOR_STEP;
    if (cursor_x < 0)         cursor_x = 0;
    if (cursor_x >= SCREEN_W) cursor_x = SCREEN_W - 1;
}

static void draw_sprite_with_key(int cx, int y_bottom,
                                 const uint16_t *sprite, int w, int h,
                                 int y_offset) {
    const int x0 = cx - (w / 2);
    const int y0 = (y_bottom + y_offset) - h;

    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            const uint16_t px = pgm_read_word(&sprite[row * w + col]);
            if (px == TRANSPARENT_KEY) {
                continue;
            }
            const int x = x0 + col;
            const int y = y0 + row;
            if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) {
                continue;
            }
            if (use_backbuffer) {
                backbuffer->drawPixel(x, y, px);
            } else {
                tft.drawPixel(x, y, px);
            }
        }
    }
}

/* ─────────────────────────────────────────────────────────────────────────── */

extern "C" {   /* game/missile_command.c is compiled as C */

void hal_init(void) {
    SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
    tft.init(TFT_WIDTH, TFT_HEIGHT);
    tft.setRotation(TFT_ROTATION);
    tft.setSPISpeed(TFT_SPI_HZ);
    tft.fillScreen(ST77XX_BLACK);

    backbuffer = new GFXcanvas16(SCREEN_W, SCREEN_H);
    use_backbuffer = (backbuffer != nullptr) && (backbuffer->getBuffer() != nullptr);

    if (use_backbuffer) {
        /* Allocate the display-copy buffer from PSRAM (8 MB on S3 DevKitC-1). */
        /* This never touches internal SRAM so it cannot cause an OOM reboot.  */
        disp_psram = (uint16_t *)ps_malloc(SCREEN_W * SCREEN_H * sizeof(uint16_t));
        if (disp_psram != nullptr) {
            memcpy(disp_psram, background_1, BG_W * BG_H * sizeof(uint16_t));
            sem_frame_ready = xSemaphoreCreateBinary();
            sem_blit_done   = xSemaphoreCreateBinary();
            xSemaphoreGive(sem_blit_done);  /* let first hal_present() proceed */
            /* Stack 8192 B, priority 1: below ESP-IDF system tasks on Core 0. */
            xTaskCreatePinnedToCore(display_task, "disp", 8192, nullptr, 1, nullptr, 0);
            use_dual_core = true;
            Serial.println("[HAL] dual-core+PSRAM OK");
        } else {
            Serial.println("[HAL] ps_malloc failed — single-buf fallback");
        }
    }

    if (TFT_BL >= 0) {
        pinMode(TFT_BL, OUTPUT);
        digitalWrite(TFT_BL, HIGH);
    }

    /* Disable onboard RGB LED (GPIO48 on ESP32-S3 DevKitC-1) */
    pinMode(48, OUTPUT);
    digitalWrite(48, LOW);

    configure_input_pin(FIRE_PIN);
    configure_input_pin(FIRE_PIN_2);
    configure_input_pin(LEFT_PIN);
    configure_input_pin(RIGHT_PIN);

    if (PIEZO_PIN >= 0) {
        ledcSetup(PIEZO_LEDC_CHANNEL, 2000, 8);
        ledcAttachPin(PIEZO_PIN, PIEZO_LEDC_CHANNEL);
        ledcWrite(PIEZO_LEDC_CHANNEL, 0);
        sound_queue = xQueueCreate(8, sizeof(int));
        if (sound_queue != nullptr) {
            xTaskCreatePinnedToCore(piezo_task, "piezo", 3072, nullptr, 1, nullptr, 1);
            use_piezo = true;
            Serial.printf("[HAL] piezo on GPIO %d\n", PIEZO_PIN);
        } else {
            Serial.println("[HAL] piezo queue alloc failed");
        }
    }

    /* ── I2S / MAX98357A audio init ──────────────────────────────────────── */
    /* Only activated when I2S_DOUT_PIN is wired.  Takes priority over piezo. */
    if (I2S_DOUT_PIN >= 0) {
        if (!SPIFFS.begin(true)) {
            Serial.println("[HAL] SPIFFS mount failed — I2S audio disabled");
        } else {
            Serial.println("[HAL] SPIFFS mounted");
            if (AMP_SD_PIN >= 0) {
                pinMode(AMP_SD_PIN, OUTPUT);
                digitalWrite(AMP_SD_PIN, HIGH); /* un-mute amp */
                Serial.printf("[HAL] AMP_SD (GPIO%d) set HIGH (enabled)\n", AMP_SD_PIN);
            } else {
                Serial.println("[HAL] AMP_SD pin not configured (tie SD high on board)");
            }

            const i2s_config_t i2s_cfg = {
                .mode                 = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX),
                .sample_rate          = kI2SSampleRate,
                .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
                .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
                .communication_format = I2S_COMM_FORMAT_STAND_I2S,
                .intr_alloc_flags     = 0,
                .dma_buf_count        = 8,
                .dma_buf_len          = 256,
                .use_apll             = false,
                .tx_desc_auto_clear   = true,
                .fixed_mclk           = 0,
            };
            const i2s_pin_config_t i2s_pins = {
                .bck_io_num   = I2S_BCLK_PIN,
                .ws_io_num    = I2S_LRCLK_PIN,
                .data_out_num = I2S_DOUT_PIN,
                .data_in_num  = I2S_PIN_NO_CHANGE,
            };
            i2s_driver_install(kI2SPort, &i2s_cfg, 0, nullptr);
            i2s_set_pin(kI2SPort, &i2s_pins);
            i2s_set_clk(kI2SPort, kI2SSampleRate,
                        I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);

            i2s_sound_queue = xQueueCreate(4, sizeof(int));
            if (i2s_sound_queue != nullptr) {
                xTaskCreatePinnedToCore(i2s_audio_task, "i2s_audio",
                                        4096, nullptr, 1, nullptr, 1);
                use_i2s_audio = true;
                Serial.printf("[HAL] I2S audio: BCLK=%d LRCLK=%d DOUT=%d AMP_SD=%d\n",
                              I2S_BCLK_PIN, I2S_LRCLK_PIN, I2S_DOUT_PIN, AMP_SD_PIN);
                
                /* Give I2S hardware time to stabilize */
                delay(100);
                
                /* Startup test beep: 1000 Hz tone for 200ms at 50% volume */
                Serial.println("[HAL] Playing startup test beep (1000 Hz, 200ms)...");
                const float test_tone_hz = 1000.0f;
                const uint32_t test_duration_ms = 200;
                const size_t test_samples = (kI2SSampleRate * test_duration_ms) / 1000;
                int16_t test_buf[512];
                size_t samples_sent = 0;
                float phase = 0.0f;
                const float phase_step = (2.0f * 3.14159f * test_tone_hz) / (float)kI2SSampleRate;
                
                while (samples_sent < test_samples) {
                    const size_t chunk = (test_samples - samples_sent > 256) ? 256 : (test_samples - samples_sent);
                    for (size_t i = 0; i < chunk; i++) {
                        const int16_t val = (int16_t)(sinf(phase) * 16384.0f); /* ~50% volume */
                        test_buf[i * 2] = val;     /* left */
                        test_buf[i * 2 + 1] = val; /* right */
                        phase += phase_step;
                        if (phase >= 2.0f * 3.14159f) phase -= 2.0f * 3.14159f;
                    }
                    size_t written;
                    i2s_write(kI2SPort, test_buf, chunk * 4, &written, portMAX_DELAY);
                    samples_sent += chunk;
                }
                Serial.printf("[HAL] Test beep complete (%u samples sent)\n", (unsigned)samples_sent);
            } else {
                Serial.println("[HAL] I2S queue alloc failed");
            }
        }
    }

    analogReadResolution(12);
    configure_analog_input_pin(JOY_X_PIN);
    configure_analog_input_pin(JOY_Y_PIN);
    calibrate_axis_center(JOY_X_PIN, &joy_center_x);
    calibrate_axis_center(JOY_Y_PIN, &joy_center_y);
    joy_filtered_x = joy_center_x;
    joy_filtered_y = joy_center_y;

    start_ms = millis();
}

void hal_clear(uint16_t color) {
    if (use_backbuffer) {
        (void)color;
        memcpy(backbuffer->getBuffer(), background_1, BG_W * BG_H * sizeof(uint16_t));
    } else {
        (void)color;
        tft.drawRGBBitmap(0, 0, background_1, BG_W, BG_H);
    }
}

void hal_draw_pixel(int x, int y, uint16_t color) {
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;
    if (use_backbuffer) backbuffer->drawPixel(x, y, color);
    else                tft.drawPixel(x, y, color);
}

void hal_draw_rect(int x, int y, int w, int h, uint16_t color) {
    if (use_backbuffer) backbuffer->fillRect(x, y, w, h, color);
    else                tft.fillRect(x, y, w, h, color);
}

void hal_draw_line(int x0, int y0, int x1, int y1, uint16_t color) {
    if (use_backbuffer) backbuffer->drawLine(x0, y0, x1, y1, color);
    else                tft.drawLine(x0, y0, x1, y1, color);
}

void hal_draw_circle(int cx, int cy, int r, uint16_t color) {
    if (use_backbuffer) backbuffer->fillCircle(cx, cy, r, color);
    else                tft.fillCircle(cx, cy, r, color);
}

void hal_draw_circle_top_half(int cx, int cy, int r, uint16_t color) {
    /* Draw filled semicircle: top half only (dy from -r to 0 from center) */
    for (int dy = -r; dy <= 0; dy++) {
        int half_w = isqrt_esp32(r * r - dy * dy);
        hal_draw_rect(cx - half_w, cy + dy, half_w * 2 + 1, 1, color);
    }
}

static uint16_t interpolate_rgb565(uint16_t color_center, uint16_t color_edge, int ratio) {
    /* ratio: 0 = all center, 255 = all edge */
    uint8_t r_c = (color_center >> 8) & 0xF8;
    uint8_t g_c = (color_center >> 3) & 0xFC;
    uint8_t b_c = (color_center << 3) & 0xF8;
    
    uint8_t r_e = (color_edge >> 8) & 0xF8;
    uint8_t g_e = (color_edge >> 3) & 0xFC;
    uint8_t b_e = (color_edge << 3) & 0xF8;
    
    uint8_t r = (r_c * (255 - ratio) + r_e * ratio) / 255;
    uint8_t g = (g_c * (255 - ratio) + g_e * ratio) / 255;
    uint8_t b = (b_c * (255 - ratio) + b_e * ratio) / 255;
    
    return RGB565(r, g, b);
}

static uint16_t blend_rgb565_toward_black(uint16_t src, uint8_t amount) {
    uint8_t r5 = (uint8_t)((src >> 11) & 0x1Fu);
    uint8_t g6 = (uint8_t)((src >> 5) & 0x3Fu);
    uint8_t b5 = (uint8_t)(src & 0x1Fu);

    uint16_t inv = (uint16_t)(255u - amount);
    uint8_t r5o = (uint8_t)((r5 * inv + 127u) / 255u);
    uint8_t g6o = (uint8_t)((g6 * inv + 127u) / 255u);
    uint8_t b5o = (uint8_t)((b5 * inv + 127u) / 255u);

    return (uint16_t)((r5o << 11) | (g6o << 5) | b5o);
}

void hal_draw_circle_top_half_gradient(int cx, int cy, int r, uint16_t color_edge) {
    /* Draw concentric half-circles from white center to color_edge */
    /* Draw from largest to smallest so white center stays on top */
    uint16_t color_white = RGB565(255, 255, 255);
    int steps = r;
    if (steps < 1) steps = 1;
    
    for (int i = steps; i >= 0; i--) {
        int ratio = i * 255 / steps;  /* 255 at edge (i=steps), 0 at center (i=0) */
        uint16_t color = interpolate_rgb565(color_white, color_edge, ratio);
        int r_i = (int)((long)i * r / steps);
        
        for (int dy = -r_i; dy <= 0; dy++) {
            int half_w = isqrt_esp32(r_i * r_i - dy * dy);
            hal_draw_rect(cx - half_w, cy + dy, half_w * 2 + 1, 1, color);
        }
    }
}

void hal_draw_circle_gradient(int cx, int cy, int r, uint16_t color_edge) {
    /* Draw concentric full circles from white center to color_edge */
    /* Draw from largest to smallest so white center stays on top */
    uint16_t color_white = RGB565(255, 255, 255);
    int steps = r;
    if (steps < 1) steps = 1;
    
    for (int i = steps; i >= 0; i--) {
        int ratio = i * 255 / steps;  /* 255 at edge (i=steps), 0 at center (i=0) */
        uint16_t color = interpolate_rgb565(color_white, color_edge, ratio);
        int r_i = (int)((long)i * r / steps);
        
        for (int dy = -r_i; dy <= r_i; dy++) {
            int half_w = isqrt_esp32(r_i * r_i - dy * dy);
            hal_draw_rect(cx - half_w, cy + dy, half_w * 2 + 1, 1, color);
        }
    }
}

void hal_draw_char(int x, int y, char c, uint16_t color, uint16_t bg) {
    if (use_backbuffer) {
        backbuffer->setCursor(x, y);
        if (bg == COL_BLACK) backbuffer->setTextColor(color);
        else                 backbuffer->setTextColor(color, bg);
        backbuffer->setTextSize(1);
        backbuffer->print(c);
    } else {
        tft.setCursor(x, y);
        if (bg == COL_BLACK) tft.setTextColor(color);
        else                 tft.setTextColor(color, bg);
        tft.setTextSize(1);
        tft.print(c);
    }
}

void hal_draw_text(int x, int y, const char *str, uint16_t color, uint16_t bg) {
    if (use_backbuffer) {
        backbuffer->setCursor(x, y);
        if (bg == COL_BLACK) backbuffer->setTextColor(color);
        else                 backbuffer->setTextColor(color, bg);
        backbuffer->setTextSize(1);
        backbuffer->print(str);
    } else {
        tft.setCursor(x, y);
        if (bg == COL_BLACK) tft.setTextColor(color);
        else                 tft.setTextColor(color, bg);
        tft.setTextSize(1);
        tft.print(str);
    }
}

void hal_draw_text_scaled(int x, int y, const char *str, uint16_t color, int scale) {
    if (use_backbuffer) {
        backbuffer->setCursor(x, y);
        backbuffer->setTextColor(color);
        backbuffer->setTextSize(scale < 1 ? 1 : scale);
        backbuffer->print(str);
    } else {
        tft.setCursor(x, y);
        tft.setTextColor(color);
        tft.setTextSize(scale < 1 ? 1 : scale);
        tft.print(str);
    }
}

void hal_present(void) {
    if (!use_backbuffer) return;
    if (use_dual_core) {
        /* Wait for Core 0 to finish the previous SPI blit. */
        xSemaphoreTake(sem_blit_done, portMAX_DELAY);
        /* Snapshot the freshly-drawn frame into the PSRAM display buffer.    */
        /* This memcpy (~153 KB) runs on Core 1 while Core 0 is idle between  */
        /* blits, so it costs nothing in wall-clock time.                     */
        memcpy(disp_psram, backbuffer->getBuffer(), SCREEN_W * SCREEN_H * sizeof(uint16_t));
        /* Signal Core 0 to start the SPI blit. */
        xSemaphoreGive(sem_frame_ready);
    } else {
        tft.drawRGBBitmap(0, 0, backbuffer->getBuffer(), SCREEN_W, SCREEN_H);
    }
}

uint8_t hal_read_input(void) {
    uint8_t b = 0;
    if (button_pressed(LEFT_PIN))  b |= BTN_LEFT;
    if (button_pressed(RIGHT_PIN)) b |= BTN_RIGHT;
    if (button_pressed(FIRE_PIN) || button_pressed(FIRE_PIN_2))  b |= BTN_FIRE;
    return b;
}

int hal_read_cursor_x(void) {
    sample_cursor_once();
    return cursor_x;
}

int hal_read_cursor_y(void) {
    sample_cursor_once();
    return cursor_y;
}

uint32_t hal_ticks_ms(void) {
    return millis() - start_ms;
}

void hal_delay_ms(uint32_t ms) {
    delay(ms);
}

void hal_draw_ground(void) {
    if (use_backbuffer) backbuffer->fillRect(0, SCREEN_H - 14, SCREEN_W, 14, g_game_cfg.ui.ground_color);
    else                tft.fillRect(0, SCREEN_H - 14, SCREEN_W, 14, g_game_cfg.ui.ground_color);
}

void hal_draw_sprite(int cx, int y_bottom, int sprite_id) {
    if (sprite_id == SPRITE_CITY) {
        draw_sprite_with_key(cx, y_bottom,
                             civilian_target, CITY_SPRITE_W, CITY_SPRITE_H,
                             CITY_SPRITE_Y_OFFSET);
    } else if (sprite_id == SPRITE_BUNKER) {
        draw_sprite_with_key(cx, y_bottom,
                             bunker_1, BUNKER_SPRITE_W, BUNKER_SPRITE_H,
                             BUNKER_SPRITE_Y_OFFSET);
    }
}

void hal_fade_to_black(uint8_t amount) {
    if (amount == 0) return;

    if (use_backbuffer) {
        uint16_t *buf = backbuffer->getBuffer();
        if (!buf) return;
        const int count = SCREEN_W * SCREEN_H;
        for (int i = 0; i < count; i++) {
            buf[i] = blend_rgb565_toward_black(buf[i], amount);
        }
    } else {
        /* Fallback path if no software framebuffer is active. */
        if (amount >= 200) {
            tft.fillRect(0, 0, SCREEN_W, SCREEN_H, COL_BLACK);
        }
    }
}

void hal_play_sound(int sound_id) {
    /* I2S audio (MAX98357A) takes priority when wired */
    if (use_i2s_audio && i2s_sound_queue != nullptr) {
        xQueueSend(i2s_sound_queue, &sound_id, 0);
        return;
    }
    /* Fallback: piezo buzzer */
    if (!use_piezo || sound_queue == nullptr) {
        (void)sound_id;
        return;
    }
    (void)xQueueSend(sound_queue, &sound_id, 0);
}

} /* extern "C" */
