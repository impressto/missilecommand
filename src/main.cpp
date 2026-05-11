/**
 * main.cpp — ESP32 Arduino entry point
 *
 * Drives the same game loop as main_wasm.c, but using Arduino's loop().
 * The game logic (missile_command.c) is identical on both platforms.
 */

#include <Arduino.h>
#include <string.h>
#include <BleMouse.h>
#include <BLEDevice.h>
#include "hal.h"              /* local to esp32/src */
#include "missile_command.h"
#include "default_startmenu_background.h"
#include "peppa_startmenu_background.h"

#ifndef JOY_X_PIN
#define JOY_X_PIN -1
#endif
#ifndef JOY_Y_PIN
#define JOY_Y_PIN -1
#endif

static uint32_t last_ticks = 0;
static uint8_t prev_menu_buttons = 0;
static uint8_t prev_mouse_buttons = 0;
static int menu_fire_armed = 0;
static uint32_t menu_enter_ms = 0;
static uint32_t menu_nav_cooldown_ms = 0;
static int prev_menu_cursor_y = SCREEN_H / 2;
static uint32_t mouse_exit_hold_ms = 0;
static uint32_t last_mouse_status_ms = 0;
static uint32_t last_ble_adv_kick_ms = 0;
static int ble_prev_connected = 0;
static uint32_t ble_connected_since_ms = 0;
static uint32_t last_mouse_report_ms = 0;
static int prev_mouse_cursor_x = SCREEN_W / 2;
static int prev_mouse_cursor_y = SCREEN_H / 2;

enum AppMode {
    APP_MENU = 0,
    APP_GAME = 1,
    APP_MOUSE = 2,
};

static AppMode app_mode = APP_MENU;
static int menu_selected = 0;

enum MenuItem {
    MENU_PLAY_CLASSIC = 0,
    MENU_PLAY_THEME_2 = 1,
    MENU_DEMO_MODE = 2,
    MENU_MOUSE_MODE = 3,
    MENU_ITEM_COUNT = 4,
};

static BleMouse ble_mouse("MissileCommand Mouse", "impressto", 100);
static int mouse_joy_center_x = 2048;
static int mouse_joy_center_y = 2048;
static int mouse_joy_calibrated = 0;

static int read_axis_avg_local(int pin, int samples) {
    if (pin < 0) return -1;
    long sum = 0;
    for (int i = 0; i < samples; i++) {
        sum += analogRead(pin);
    }
    return (int)(sum / samples);
}

static void calibrate_mouse_joystick_center() {
    if (JOY_X_PIN < 0 || JOY_Y_PIN < 0) {
        mouse_joy_calibrated = 0;
        return;
    }

    long sx = 0;
    long sy = 0;
    const int samples = 24;
    for (int i = 0; i < samples; i++) {
        sx += analogRead(JOY_X_PIN);
        sy += analogRead(JOY_Y_PIN);
        delay(2);
    }
    mouse_joy_center_x = (int)(sx / samples);
    mouse_joy_center_y = (int)(sy / samples);
    mouse_joy_calibrated = 1;
}

static int text_pixel_width(const char *str) {
    const int len = (int)strlen(str);
    return (len > 0) ? (6 * len - 1) : 0;
}

static void draw_text_centered(int y, const char *str, uint16_t color, uint16_t bg) {
    const int x = (SCREEN_W - text_pixel_width(str)) / 2;
    hal_draw_text(x, y, str, color, bg);
}

static void draw_text_scaled_centered(int y, const char *str, uint16_t color, int scale) {
    const int s = (scale < 1) ? 1 : scale;
    const int x = (SCREEN_W - (text_pixel_width(str) * s)) / 2;
    hal_draw_text_scaled(x, y, str, color, s);
}

static void draw_start_menu() {
    const int theme2_preview = (menu_selected == MENU_PLAY_THEME_2);
    hal_set_theme(theme2_preview ? THEME_ALTERNATE : THEME_CLASSIC);
    hal_draw_rgb565_background(theme2_preview ? peppa_startmenu_background_1 : default_startmenu_background_1);

    const uint16_t classic_col = (menu_selected == MENU_PLAY_CLASSIC) ? COL_GREEN : COL_WHITE;
    const uint16_t theme2_col = (menu_selected == MENU_PLAY_THEME_2) ? COL_ORANGE : COL_WHITE;
    const uint16_t demo_col = (menu_selected == MENU_DEMO_MODE) ? COL_YELLOW : COL_WHITE;
    const uint16_t mouse_col = (menu_selected == MENU_MOUSE_MODE) ? COL_CYAN : COL_WHITE;

    if (theme2_preview) {
        draw_text_scaled_centered(84, "THEME 2", COL_ORANGE, 2);
        draw_text_centered(102, "NEON ASSAULT PROFILE", COL_CYAN, COL_BLACK);
        draw_text_centered(118, (menu_selected == MENU_PLAY_CLASSIC) ? "> PLAY CLASSIC" : "  PLAY CLASSIC", classic_col, COL_BLACK);
        draw_text_scaled_centered(132, (menu_selected == MENU_PLAY_THEME_2) ? "> PLAY THEME 2" : "  PLAY THEME 2", theme2_col, 2);
        draw_text_centered(154, (menu_selected == MENU_DEMO_MODE) ? "> DEMO MODE" : "  DEMO MODE", demo_col, COL_BLACK);
        draw_text_centered(170, (menu_selected == MENU_MOUSE_MODE) ? "> MOUSE MODE" : "  MOUSE MODE", mouse_col, COL_BLACK);
    } else {
        draw_text_scaled_centered(84, "CLASSIC", COL_GREEN, 2);
        draw_text_centered(102, "ORIGINAL DEFENSE PROFILE", COL_WHITE, COL_BLACK);
        draw_text_centered(118, (menu_selected == MENU_PLAY_CLASSIC) ? "> PLAY CLASSIC" : "  PLAY CLASSIC", classic_col, COL_BLACK);
        draw_text_centered(134, (menu_selected == MENU_PLAY_THEME_2) ? "> PLAY THEME 2" : "  PLAY THEME 2", theme2_col, COL_BLACK);
        draw_text_centered(150, (menu_selected == MENU_DEMO_MODE) ? "> DEMO MODE" : "  DEMO MODE", demo_col, COL_BLACK);
        draw_text_centered(166, (menu_selected == MENU_MOUSE_MODE) ? "> MOUSE MODE" : "  MOUSE MODE", mouse_col, COL_BLACK);
    }
    hal_present();
}

static void update_start_menu(uint32_t now) {
    const uint8_t buttons = hal_read_input();
    (void)hal_read_cursor_x();
    const int cy = hal_read_cursor_y();
    const uint32_t menu_nav_repeat_ms = 170;
    const int menu_nav_delta_px = 3;

    int nav_step = 0;
    int delta_y = cy - prev_menu_cursor_y;

    if (delta_y <= -menu_nav_delta_px) {
        nav_step = -1;
    } else if (delta_y >= menu_nav_delta_px) {
        nav_step = 1;
    }

    if ((buttons & BTN_LEFT) && !(prev_menu_buttons & BTN_LEFT)) {
        nav_step = -1;
    }
    if ((buttons & BTN_RIGHT) && !(prev_menu_buttons & BTN_RIGHT)) {
        nav_step = 1;
    }

    if (nav_step != 0 && (now - menu_nav_cooldown_ms) >= menu_nav_repeat_ms) {
        menu_selected += nav_step;
        if (menu_selected < 0) menu_selected = 0;
        if (menu_selected >= MENU_ITEM_COUNT) menu_selected = MENU_ITEM_COUNT - 1;
        menu_nav_cooldown_ms = now;
    }

    prev_menu_cursor_y = cy;

    if (!menu_fire_armed) {
        if ((buttons & BTN_FIRE) == 0 && (now - menu_enter_ms) > 300) {
            menu_fire_armed = 1;
        }
    }

    if (menu_fire_armed && (buttons & BTN_FIRE) && !(prev_menu_buttons & BTN_FIRE)) {
        if (menu_selected == MENU_MOUSE_MODE) {
            app_mode = APP_MOUSE;
            prev_mouse_buttons = 0;
            mouse_exit_hold_ms = 0;
            last_mouse_status_ms = 0;
            last_ble_adv_kick_ms = 0;
            ble_connected_since_ms = 0;
            last_mouse_report_ms = 0;
            prev_mouse_cursor_x = hal_read_cursor_x();
            prev_mouse_cursor_y = hal_read_cursor_y();
            calibrate_mouse_joystick_center();
        } else {
            const int selected_theme = (menu_selected == MENU_PLAY_THEME_2) ? THEME_ALTERNATE : THEME_CLASSIC;
            hal_set_theme(selected_theme);
            game_set_demo_mode(menu_selected == MENU_DEMO_MODE);
            game_init();
            last_ticks = hal_ticks_ms();
            app_mode = APP_GAME;
        }
    }

    prev_menu_buttons = buttons;
    draw_start_menu();
    hal_play_startup_wav_once();
}

static void draw_mouse_mode_screen(bool connected) {
    hal_clear(COL_BLACK);
    hal_draw_rect(14, 18, SCREEN_W - 28, 196, COL_BLACK);
    draw_text_centered(34, "MISSILE COMMAND", COL_CYAN, COL_BLACK);
    draw_text_centered(56, "MOUSE MODE", COL_WHITE, COL_BLACK);
    draw_text_centered(96, connected ? "BLE STATUS: CONNECTED" : "BLE STATUS: WAITING", connected ? COL_GREEN : COL_YELLOW, COL_BLACK);
    draw_text_centered(120, "Joystick = mouse move", COL_WHITE, COL_BLACK);
    draw_text_centered(136, "FIRE = left click", COL_WHITE, COL_BLACK);
    draw_text_centered(152, "LEFT button = right click", COL_WHITE, COL_BLACK);
    draw_text_centered(176, "Hold FIRE+LEFT+DOWN for 2.5s to exit", COL_GRAY, COL_BLACK);
    hal_present();
}

static void update_mouse_mode(uint32_t now) {
    const uint8_t buttons = hal_read_input();
    const int cx = hal_read_cursor_x();
    const int cy = hal_read_cursor_y();
    const bool connected = ble_mouse.isConnected();

    if (!connected && (now - last_ble_adv_kick_ms) >= 2000) {
        BLEDevice::startAdvertising();
        last_ble_adv_kick_ms = now;
    }

    if ((connected ? 1 : 0) != ble_prev_connected) {
        Serial.printf("[BLE] mouse %s\n", connected ? "connected" : "disconnected");
        if (connected) {
            ble_connected_since_ms = now;
            last_mouse_report_ms = 0;
        }
        ble_prev_connected = connected ? 1 : 0;
    }

    if (connected) {
        const bool reports_ready = (now - ble_connected_since_ms) >= 900;
        const bool report_slot_ready = (now - last_mouse_report_ms) >= 20;
        int dx = 0;
        int dy = 0;

        if (mouse_joy_calibrated && JOY_X_PIN >= 0 && JOY_Y_PIN >= 0) {
            const int raw_x = read_axis_avg_local(JOY_X_PIN, 4);
            const int raw_y = read_axis_avg_local(JOY_Y_PIN, 4);
            int off_x = raw_x - mouse_joy_center_x;
            int off_y = raw_y - mouse_joy_center_y;
            const int dead = 170;

            if (off_x > -dead && off_x < dead) off_x = 0;
            if (off_y > -dead && off_y < dead) off_y = 0;

            dx = off_x / 160;
            dy = off_y / 160;
        } else {
            const int delta_deadband = 1;
            dx = cx - prev_mouse_cursor_x;
            dy = cy - prev_mouse_cursor_y;
            prev_mouse_cursor_x = cx;
            prev_mouse_cursor_y = cy;
            if (dx > -delta_deadband && dx < delta_deadband) dx = 0;
            if (dy > -delta_deadband && dy < delta_deadband) dy = 0;
            dx *= 2;
            dy *= 2;
        }

        if (dx > 12) dx = 12;
        if (dx < -12) dx = -12;
        if (dy > 12) dy = 12;
        if (dy < -12) dy = -12;

        if (reports_ready && report_slot_ready && (dx != 0 || dy != 0)) {
            ble_mouse.move(dx, dy, 0, 0);
            last_mouse_report_ms = now;
        }

        if (reports_ready && (buttons & BTN_FIRE) && !(prev_mouse_buttons & BTN_FIRE)) {
            ble_mouse.click(MOUSE_LEFT);
            last_mouse_report_ms = now;
        }

        if (reports_ready && (buttons & BTN_LEFT) && !(prev_mouse_buttons & BTN_LEFT)) {
            ble_mouse.click(MOUSE_RIGHT);
            last_mouse_report_ms = now;
        }
    }

    if ((buttons & BTN_FIRE) && (buttons & BTN_LEFT) && cy > (SCREEN_H - 8)) {
        if (mouse_exit_hold_ms == 0) {
            mouse_exit_hold_ms = now;
        } else if ((now - mouse_exit_hold_ms) >= 2500) {
            if (connected) {
                ble_mouse.release(MOUSE_LEFT);
                ble_mouse.release(MOUSE_RIGHT);
            }
            app_mode = APP_MENU;
            menu_enter_ms = now;
            menu_fire_armed = 0;
            menu_nav_cooldown_ms = now;
            prev_menu_cursor_y = hal_read_cursor_y();
            prev_menu_buttons = 0;
            prev_mouse_buttons = 0;
            return;
        }
    } else {
        mouse_exit_hold_ms = 0;
    }

    if ((now - last_mouse_status_ms) >= 100) {
        draw_mouse_mode_screen(connected);
        last_mouse_status_ms = now;
    }

    prev_mouse_buttons = buttons;
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
    ble_mouse.begin();
    calibrate_mouse_joystick_center();
    ble_prev_connected = 0;
    last_ble_adv_kick_ms = 0;
    ble_connected_since_ms = 0;
    last_mouse_report_ms = 0;
    prev_mouse_cursor_x = hal_read_cursor_x();
    prev_mouse_cursor_y = hal_read_cursor_y();
    last_ticks = hal_ticks_ms();
    menu_enter_ms = last_ticks;
    menu_nav_cooldown_ms = last_ticks;
    prev_menu_cursor_y = hal_read_cursor_y();
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

    if (app_mode == APP_MOUSE) {
        update_mouse_mode(now);
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
        menu_nav_cooldown_ms = menu_enter_ms;
        prev_menu_cursor_y = hal_read_cursor_y();
        menu_fire_armed = 0;
    }
}
