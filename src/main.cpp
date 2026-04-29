/**
 * main.cpp — ESP32 Arduino entry point
 *
 * Drives the same game loop as main_wasm.c, but using Arduino's loop().
 * The game logic (missile_command.c) is identical on both platforms.
 */

#include <Arduino.h>
#include "hal.h"              /* local to esp32/src */
#include "missile_command.h"

#ifndef JOY_X_PIN
#define JOY_X_PIN -1
#endif
#ifndef JOY_Y_PIN
#define JOY_Y_PIN -1
#endif

static uint32_t last_ticks = 0;
static uint8_t prev_menu_buttons = 0;
static int menu_fire_armed = 0;
static uint32_t menu_enter_ms = 0;

enum AppMode {
    APP_MENU = 0,
    APP_GAME = 1,
};

static AppMode app_mode = APP_MENU;
static int menu_demo_selected = 0;

static void draw_start_menu() {
    hal_clear(COL_BLACK);
    hal_draw_rect(14, 18, SCREEN_W - 28, 196, COL_BLACK);
    hal_draw_text(92, 34, "MISSILE COMMAND", COL_CYAN, COL_BLACK);
    hal_draw_text(90, 56, "ESP32 START MENU", COL_WHITE, COL_BLACK);

    const uint16_t play_col = menu_demo_selected ? COL_WHITE : COL_GREEN;
    const uint16_t demo_col = menu_demo_selected ? COL_YELLOW : COL_WHITE;
    hal_draw_text(54, 108, menu_demo_selected ? "  PLAY NOW" : "> PLAY NOW", play_col, COL_BLACK);
    hal_draw_text(54, 124, menu_demo_selected ? "> DEMO MODE" : "  DEMO MODE", demo_col, COL_BLACK);

    hal_draw_text(10, 174, "Move joystick left/right or up/down", COL_GRAY, COL_BLACK);
    if (menu_fire_armed) {
        hal_draw_text(36, 188, "Press fire button to start", COL_GRAY, COL_BLACK);
    } else {
        hal_draw_text(44, 188, "Release fire to arm menu", COL_GRAY, COL_BLACK);
    }
    hal_present();
}

static void update_start_menu(uint32_t now) {
    const uint8_t buttons = hal_read_input();
    const int cx = hal_read_cursor_x();
    const int cy = hal_read_cursor_y();
    const int center_x = SCREEN_W / 2;
    const int center_y = SCREEN_H / 2;
    const int menu_axis_deadband = 16;

    if (cx < center_x - menu_axis_deadband) {
        menu_demo_selected = 0;
    } else if (cx > center_x + menu_axis_deadband) {
        menu_demo_selected = 1;
    }

    if (cy < center_y - menu_axis_deadband) {
        menu_demo_selected = 0;
    } else if (cy > center_y + menu_axis_deadband) {
        menu_demo_selected = 1;
    }

    if ((buttons & BTN_LEFT) && !(prev_menu_buttons & BTN_LEFT)) {
        menu_demo_selected = 0;
    }
    if ((buttons & BTN_RIGHT) && !(prev_menu_buttons & BTN_RIGHT)) {
        menu_demo_selected = 1;
    }

    if (!menu_fire_armed) {
        if ((buttons & BTN_FIRE) == 0 && (now - menu_enter_ms) > 300) {
            menu_fire_armed = 1;
        }
    }

    if (menu_fire_armed && (buttons & BTN_FIRE) && !(prev_menu_buttons & BTN_FIRE)) {
        game_set_demo_mode(menu_demo_selected);
        game_init();
        last_ticks = hal_ticks_ms();
        app_mode = APP_GAME;
    }

    prev_menu_buttons = buttons;
    draw_start_menu();
}
#ifdef JOYSTICK_TEST
static uint32_t last_joy_log_ms = 0;
static uint32_t last_joy_hint_ms = 0;
static int joy_min_x = 4095;
static int joy_max_x = 0;
static int joy_min_y = 4095;
static int joy_max_y = 0;

static int read_axis_avg(int pin) {
    if (pin < 0) return -1;
    long sum = 0;
    const int samples = 8;
    for (int i = 0; i < samples; i++) {
        sum += analogRead(pin);
    }
    return (int)(sum / samples);
}

static void log_joystick_test(uint32_t now) {
    if (now - last_joy_log_ms >= 150) {
        const int cx = hal_read_cursor_x();
        const int cy = hal_read_cursor_y();
        const uint8_t b = hal_read_input();
        const int raw_x = read_axis_avg(JOY_X_PIN);
        const int raw_y = read_axis_avg(JOY_Y_PIN);
        if (raw_x >= 0) {
            if (raw_x < joy_min_x) joy_min_x = raw_x;
            if (raw_x > joy_max_x) joy_max_x = raw_x;
        }
        if (raw_y >= 0) {
            if (raw_y < joy_min_y) joy_min_y = raw_y;
            if (raw_y > joy_max_y) joy_max_y = raw_y;
        }
        Serial.printf("JOY raw=(%d,%d) rangeX=[%d..%d] rangeY=[%d..%d] cursor=(%d,%d) btn=0x%02X\n",
                      raw_x, raw_y, joy_min_x, joy_max_x, joy_min_y, joy_max_y, cx, cy, b);
        last_joy_log_ms = now;
    }
    if (now - last_joy_hint_ms >= 2000) {
        const int span_x = joy_max_x - joy_min_x;
        const int span_y = joy_max_y - joy_min_y;
        Serial.printf("CAL recommended flags: -DJOY_CAL_X_MIN=%d -DJOY_CAL_X_MAX=%d -DJOY_CAL_Y_MIN=%d -DJOY_CAL_Y_MAX=%d (spanX=%d spanY=%d)\n",
                      joy_min_x, joy_max_x, joy_min_y, joy_max_y, span_x, span_y);
        last_joy_hint_ms = now;
    }
}
#endif

void setup() {
    Serial.begin(115200);
    uint32_t serial_wait_start = millis();
    while (!Serial && (millis() - serial_wait_start) < 2000) {
        delay(10);
    }
    Serial.println("[BOOT] Serial ready at 115200");
    hal_init();
    last_ticks = hal_ticks_ms();
    menu_enter_ms = last_ticks;
    menu_fire_armed = 0;
#ifdef JOYSTICK_TEST
    Serial.println("[JOYSTICK_TEST] enabled: calibration logging active");
    Serial.println("[JOYSTICK_TEST] Move stick to far LEFT/RIGHT/UP/DOWN repeatedly for ~10s.");
    Serial.println("[JOYSTICK_TEST] Then copy the printed -DJOY_CAL_* flags into platformio.ini build_flags.");
#endif
}

void loop() {
    uint32_t now   = hal_ticks_ms();

#ifdef JOYSTICK_TEST
    log_joystick_test(now);
#endif

    if (app_mode == APP_MENU) {
        update_start_menu(now);
        return;
    }

    uint32_t delta = now - last_ticks;

    /* In dual-core mode hal_present() is naturally paced by the SPI blit;   */
    /* keep only a loose cap so delta never hits zero on very fast iterations. */
    if (delta < 16) return;  /* ~60 fps ceiling; SPI pipeline sets real rate  */
    last_ticks = now;
    if (delta > 100) delta = 100;   /* safety cap */

    int done = game_update(delta);
    game_render();

    if (done) {
        /* Game over — wait for a button press then restart */
        delay(2000);
        app_mode = APP_MENU;
        prev_menu_buttons = 0;
        menu_enter_ms = hal_ticks_ms();
        menu_fire_armed = 0;
    }
}
