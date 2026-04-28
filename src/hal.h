/**
 * hal.h — Hardware Abstraction Layer for Missile Command
 *
 * This header defines the ONLY interface between game logic and hardware.
 * To port to a new platform, implement every function below for that platform.
 *
 * Coordinate system: (0,0) = top-left
 * Colour format:     RGB565 (matches ILI9341 natively, cheap to convert in WebGL/Canvas)
 */

#ifndef HAL_H
#define HAL_H

#include <stdint.h>

/* Avoid collision with ESP-IDF/Arduino internal hal_init symbol. */
#define hal_init mc_hal_init

#ifdef __cplusplus
extern "C" {
#endif

/* ── Display dimensions ──────────────────────────────────────────────────── */
/* ILI9341 in landscape orientation */
#define SCREEN_W 320
#define SCREEN_H 240

/* ── Colour helpers (RGB888 → RGB565) ────────────────────────────────────── */
#define RGB565(r, g, b) ((uint16_t)(((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

#define COL_BLACK   RGB565(0,   0,   0)
#define COL_WHITE   RGB565(255, 255, 255)
#define COL_RED     RGB565(255, 0,   0)
#define COL_GREEN   RGB565(0,   255, 0)
#define COL_BLUE    RGB565(0,   0,   255)
#define COL_YELLOW  RGB565(255, 255, 0)
#define COL_CYAN    RGB565(0,   255, 255)
#define COL_ORANGE  RGB565(255, 128, 0)
#define COL_GRAY    RGB565(128, 128, 128)
#define COL_DKGREEN RGB565(0,   128, 0)
#define COL_DKBLUE  RGB565(0,   0,   128)

/* ── Input button bitmask ────────────────────────────────────────────────── */
#define BTN_LEFT   (1u << 0)
#define BTN_RIGHT  (1u << 1)
#define BTN_FIRE   (1u << 2)

/* ══════════════════════════════════════════════════════════════════════════
 * Platform functions — implement these for each target
 * ══════════════════════════════════════════════════════════════════════════ */

/**
 * hal_init() — one-time hardware/display initialisation.
 * Called once at startup before any other HAL function.
 */
void hal_init(void);

/**
 * hal_clear(color) — fill the entire framebuffer with a solid colour.
 */
void hal_clear(uint16_t color);

/**
 * hal_draw_pixel(x, y, color) — set a single pixel.
 */
void hal_draw_pixel(int x, int y, uint16_t color);

/**
 * hal_draw_rect(x, y, w, h, color) — filled rectangle.
 */
void hal_draw_rect(int x, int y, int w, int h, uint16_t color);

/**
 * hal_draw_line(x0, y0, x1, y1, color) — Bresenham line.
 */
void hal_draw_line(int x0, int y0, int x1, int y1, uint16_t color);

/**
 * hal_draw_circle(cx, cy, r, color) — filled circle.
 */
void hal_draw_circle(int cx, int cy, int r, uint16_t color);

/**
 * hal_draw_char(x, y, c, color, bg) — draw a single ASCII character (5×7 font).
 * bg = COL_BLACK means transparent (no background drawn) is up to the impl.
 */
void hal_draw_char(int x, int y, char c, uint16_t color, uint16_t bg);

/**
 * hal_draw_text(x, y, str, color, bg) — draw a null-terminated string.
 */
void hal_draw_text(int x, int y, const char *str, uint16_t color, uint16_t bg);

/* ── Sprite IDs ──────────────────────────────────────────────────────────── */
#define SPRITE_CITY    0   /* civilian target building */
#define SPRITE_BUNKER  1   /* missile battery / bunker */

/**
 * hal_draw_text_scaled(x, y, str, color, scale) — draw a string using the
 * same 5×7 font as hal_draw_text but with each pixel rendered as a
 * scale×scale block.  scale=1 is identical to hal_draw_text.
 */
void hal_draw_text_scaled(int x, int y, const char *str, uint16_t color, int scale);

/**
 * hal_draw_sprite(cx, y_bottom, sprite_id) — draw a sprite centred on cx,
 * with its bottom edge at y_bottom.  Transparent pixels are not drawn.
 * Falls back to the original coloured rectangles on platforms without images.
 */
void hal_draw_sprite(int cx, int y_bottom, int sprite_id);

/**
 * hal_draw_ground() — draw the coloured ground strip at the bottom of the
 * screen.  Called once per frame, after hal_clear().  Colour and height are
 * platform-specific (configurable in config.h for the WebAssembly build).
 */
void hal_draw_ground(void);

/**
 * hal_present() — flush the framebuffer to the display.
 * On displays with direct pixel access this may be a no-op.
 * On WebAssembly this triggers a canvas repaint.
 */
void hal_present(void);

/**
 * hal_read_input() — returns the current button state as a bitmask (BTN_*).
 * On ESP32 reads GPIO / ADC; on WebAssembly reads keyboard/mouse state.
 * This is a snapshot — call once per frame.
 */
uint8_t hal_read_input(void);

/**
 * hal_read_cursor_x() — returns the current cursor/crosshair X position.
 * Range: [0, SCREEN_W-1].
 * On ESP32 comes from joystick ADC; on WebAssembly from mouse position.
 */
int hal_read_cursor_x(void);

/**
 * hal_read_cursor_y() — returns the current cursor/crosshair Y position.
 * Range: [0, SCREEN_H-1].
 */
int hal_read_cursor_y(void);

/**
 * hal_ticks_ms() — milliseconds since hal_init() was called.
 * Used for frame timing and animation deltas.
 */
uint32_t hal_ticks_ms(void);

/**
 * hal_delay_ms(ms) — busy/sleep delay.  Avoid calling inside the game loop;
 * prefer frame-rate limiting via hal_ticks_ms().
 */
void hal_delay_ms(uint32_t ms);

/* ── Sound IDs ───────────────────────────────────────────────────────────── */
#define SND_LAUNCH        0   /* player missile fired           */
#define SND_PLAYER_BURST  1   /* player missile reaches target  */
#define SND_INTERCEPT     2   /* enemy missile destroyed        */
#define SND_IMPACT        3   /* enemy missile hits ground/city */
#define SND_ALERT         4   /* enemy missile spawned          */
#define SND_WAVE_COMPLETE 5   /* wave clears, next wave starts  */
#define SND_GAME_OVER     6   /* all cities destroyed           */

/**
 * hal_play_sound(sound_id) — play a sound effect (fire-and-forget).
 * On WebAssembly, plays the corresponding MP3 via the Web Audio API.
 * On ESP32, this is a no-op stub (no speaker in base hardware).
 */
void hal_play_sound(int sound_id);

#ifdef __cplusplus
}
#endif

#endif /* HAL_H */
