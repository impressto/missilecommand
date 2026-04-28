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

static void main_loop(void) {
    uint32_t now   = hal_ticks_ms();
    uint32_t delta = now - last_ticks;
    last_ticks = now;

    /* Cap delta to prevent spiral-of-death after tab focus loss */
    if (delta > 100) delta = 100;

    int done = game_update(delta);
    game_render();

    if (done) {
        /* Keep rendering the game-over screen; input would restart the game.
           For now, stop the main loop. Students can add a restart feature. */
        emscripten_cancel_main_loop();
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
