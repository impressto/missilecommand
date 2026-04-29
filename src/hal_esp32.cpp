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

#include "game_config.h"
#include "background-1.h"
#include "civilian-target.h"
#include "bunker-1.h"

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

    configure_input_pin(FIRE_PIN);
    configure_input_pin(FIRE_PIN_2);
    configure_input_pin(LEFT_PIN);
    configure_input_pin(RIGHT_PIN);

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

void hal_play_sound(int sound_id) {
    /* No speaker in base hardware; silence is fine. */
    (void)sound_id;
}

} /* extern "C" */
