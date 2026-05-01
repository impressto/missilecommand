/**
 * main_wasm.c — Emscripten entry point
 *
 * Sets up the game, then drives the game loop via emscripten_set_main_loop()
 * so the browser's requestAnimationFrame is used (no busy-loop).
 */

#include <emscripten.h>
#include "../game/hal.h"
#include "../game/missile_command.h"

static uint32_t last_ticks = 0;
static uint8_t  restart_prev_buttons = 0;
static uint8_t  restart_wait_for_release = 0;

#define RESTART_BTN_X 124
#define RESTART_BTN_Y 126
#define RESTART_BTN_W 72
#define RESTART_BTN_H 14

static void main_loop(void) {
    uint32_t now   = hal_ticks_ms();
    uint32_t delta = now - last_ticks;
    last_ticks = now;

    /* Cap delta to prevent spiral-of-death after tab focus loss */
    if (delta > 100) delta = 100;

    /* Swallow FIRE after a restart click so it does not launch a missile. */
    if (restart_wait_for_release) {
        uint8_t buttons = hal_read_input();
        if (!(buttons & BTN_FIRE)) {
            restart_wait_for_release = 0;
        }
        game_render();
        return;
    }

    int done = game_update(delta);
    game_render();

    if (done) {
        uint8_t buttons = hal_read_input();
        uint8_t fire_now = (uint8_t)(buttons & BTN_FIRE);
        uint8_t fire_prev = (uint8_t)(restart_prev_buttons & BTN_FIRE);
        int cursor_x = hal_read_cursor_x();
        int cursor_y = hal_read_cursor_y();
        int in_restart_button =
            (cursor_x >= RESTART_BTN_X) && (cursor_x < (RESTART_BTN_X + RESTART_BTN_W)) &&
            (cursor_y >= RESTART_BTN_Y) && (cursor_y < (RESTART_BTN_Y + RESTART_BTN_H));

        /* Restart only on a fresh click/tap inside the Restart button area. */
        if (fire_now && !fire_prev && in_restart_button) {
            game_init();
            last_ticks = hal_ticks_ms();
            restart_wait_for_release = 1;
        }
        restart_prev_buttons = buttons;
    } else {
        restart_prev_buttons = 0;
    }
}

int main(void) {
    hal_init();
    game_init();
    last_ticks = hal_ticks_ms();

    /* 0 fps = use requestAnimationFrame (browser controls timing) */
    emscripten_set_main_loop(main_loop, 0, 1);
    return 0;
}
