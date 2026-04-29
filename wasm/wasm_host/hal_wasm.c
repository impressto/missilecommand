/**
 * hal_wasm.c — WebAssembly (Emscripten) implementation of hal.h
 *
 * Renders to an HTML5 <canvas> via JavaScript calls injected by Emscripten.
 * The canvas is 320×240 (same as ILI9341) so the ESP32 port is pixel-perfect.
 *
 * Font: a minimal 5×7 ASCII bitmap font stored in flash-friendly uint8 arrays.
 */

#include "../game/hal.h"
#include "lodepng_wrapper.h"
#include "config.h"

#include <emscripten.h>
#include <emscripten/html5.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

/* ── Forward declarations ────────────────────────────────────────────────── */
static int isqrt_approx(int n);

/* ── Framebuffer ─────────────────────────────────────────────────────────── */
/* We keep a software framebuffer in WASM memory and push it to the canvas
   on hal_present().  This mirrors how the ILI9341 driver works (block writes)
   and makes the rendering path identical on both platforms.               */
static uint16_t fb[SCREEN_H][SCREEN_W];

/* ── Background image (decoded once from PNG, scaled to 320×240 RGB565) ─── */
static uint16_t bg_fb[SCREEN_H][SCREEN_W];
static int      bg_loaded = 0;

/* ── Sprite store ────────────────────────────────────────────────────────── */
/* Each sprite is stored as RGBA8888 at its draw dimensions (see config.h). */
#define MAX_SPRITES     2
#define MAX_SPRITE_W    ((CITY_SPRITE_W > BUNKER_SPRITE_W) ? CITY_SPRITE_W : BUNKER_SPRITE_W)
#define MAX_SPRITE_H    ((CITY_SPRITE_H > BUNKER_SPRITE_H) ? CITY_SPRITE_H : BUNKER_SPRITE_H)
static uint8_t  sprite_data[MAX_SPRITES][MAX_SPRITE_H][MAX_SPRITE_W][4];
static int      sprite_loaded[MAX_SPRITES];

/* ── Input state (written from JS via EM_JS callbacks) ───────────────────── */
static int      g_cursor_x   = SCREEN_W / 2;
static int      g_cursor_y   = SCREEN_H / 2;
static uint8_t  g_buttons    = 0;
static uint8_t  g_fire_pulse = 0;
static uint32_t g_start_ms   = 0;

static const int sprite_w[MAX_SPRITES] = {
    CITY_SPRITE_W,
    BUNKER_SPRITE_W,
};

static const int sprite_h[MAX_SPRITES] = {
    CITY_SPRITE_H,
    BUNKER_SPRITE_H,
};

static const int sprite_y_offset[MAX_SPRITES] = {
    CITY_SPRITE_Y_OFFSET,
    BUNKER_SPRITE_Y_OFFSET,
};

/* ════════════════════════════════════════════════════════════════════════════
 * JavaScript helpers injected into the WASM module
 * ════════════════════════════════════════════════════════════════════════════ */

/* Copy the RGB565 framebuffer to the canvas via an ImageData transfer */
EM_JS(void, js_blit_framebuffer, (const uint16_t *fb_ptr, int w, int h), {
    const canvas  = document.getElementById('gameCanvas');
    const ctx     = canvas.getContext('2d');
    const imgData = ctx.createImageData(w, h);
    const src     = new Uint16Array(wasmMemory.buffer, fb_ptr, w * h);
    const dst     = imgData.data;
    for (let i = 0; i < w * h; i++) {
        const p = src[i];
        dst[i * 4 + 0] = (p >> 8) & 0xF8;          /* R */
        dst[i * 4 + 1] = (p >> 3) & 0xFC;          /* G */
        dst[i * 4 + 2] = (p << 3) & 0xF8;          /* B */
        dst[i * 4 + 3] = 255;                       /* A */
    }
    ctx.putImageData(imgData, 0, 0);
});

/* Register canvas mouse-move and click listeners (called once from hal_init) */
EM_JS(void, js_setup_input, (void), {
    const canvas = document.getElementById('gameCanvas');

    function canvasPointFromEvent(e) {
        const r = canvas.getBoundingClientRect();
        const x = Math.floor((e.clientX - r.left) / (r.width  / canvas.width));
        const y = Math.floor((e.clientY - r.top)  / (r.height / canvas.height));
        return { x, y };
    }

    canvas.addEventListener('mousemove', function(e) {
        const { x, y } = canvasPointFromEvent(e);
        Module._hal_wasm_set_cursor(x, y);
    });

    canvas.addEventListener('mousedown', function(e) {
        Module._hal_wasm_set_fire(1);
    });
    canvas.addEventListener('mouseup', function(e) {
        Module._hal_wasm_set_fire(0);
    });

    /* Mobile touch: a touch both aims and fires in a single event. */
    function handleTouch(e, fire) {
        if (!e.touches || e.touches.length === 0) return;
        const t = e.touches[0];
        const { x, y } = canvasPointFromEvent(t);
        if (fire) Module._hal_wasm_tap_at(x, y);
        else Module._hal_wasm_set_cursor(x, y);
    }

    canvas.addEventListener('touchstart', function(e) {
        e.preventDefault();
        handleTouch(e, true);
    }, { passive: false });

    canvas.addEventListener('touchmove', function(e) {
        e.preventDefault();
        handleTouch(e, false);
    }, { passive: false });

    /* Keyboard: arrow keys move a virtual cursor for keyboard-only play */
    document.addEventListener('keydown', function(e) {
        if (e.code === 'Space' || e.code === 'KeyZ') Module._hal_wasm_set_fire(1);
    });
    document.addEventListener('keyup', function(e) {
        if (e.code === 'Space' || e.code === 'KeyZ') Module._hal_wasm_set_fire(0);
    });
});

/* Play one-shot sound effect from preloaded assets in the Emscripten FS. */
EM_JS(void, js_play_sound, (int sound_id), {
    const SOUND_FILES = [
        '/missile-2.mp3',   /* SND_LAUNCH */
        '/swoop-up.mp3',    /* SND_PLAYER_BURST */
        '/explosions.mp3',  /* SND_INTERCEPT */
        '/explode.mp3',     /* SND_IMPACT */
        '/alert.mp3',       /* SND_ALERT */
        '/roll-up.mp3',     /* SND_WAVE_COMPLETE */
        '/finale.mp3'       /* SND_GAME_OVER */
    ];

    if (sound_id < 0 || sound_id >= SOUND_FILES.length) return;

    const path = SOUND_FILES[sound_id];
    const volume = (typeof Module._masterVolume === 'number') ? Module._masterVolume : 0.7;

    if (!Module._soundTemplateCache) Module._soundTemplateCache = {};
    if (!Module._soundBlobUrlCache) Module._soundBlobUrlCache = {};

    let template = Module._soundTemplateCache[path];
    if (!template) {
        try {
            const bytes = FS.readFile(path, { encoding: 'binary' });
            const blob = new Blob([bytes], { type: 'audio/mpeg' });
            const url = URL.createObjectURL(blob);
            Module._soundBlobUrlCache[path] = url;

            template = new Audio(url);
            template.preload = 'auto';
            Module._soundTemplateCache[path] = template;
        } catch (e) {
            console.warn('Failed to load sound:', path, e);
            return;
        }
    }

    const voice = template.cloneNode();
    voice.volume = Math.max(0, Math.min(1, volume));
    voice.play().catch(function() {
        /* Autoplay policies may block play until user input; ignore silently. */
    });
});

/* Exported C functions called from JS (must be kept with EMSCRIPTEN_KEEPALIVE) */
EMSCRIPTEN_KEEPALIVE void hal_wasm_set_cursor(int x, int y) {
    if (x < 0) x = 0;
    if (x >= SCREEN_W) x = SCREEN_W - 1;
    if (y < 0) y = 0;
    if (y >= SCREEN_H) y = SCREEN_H - 1;
    g_cursor_x = x;
    g_cursor_y = y;
}

EMSCRIPTEN_KEEPALIVE void hal_wasm_set_fire(int pressed) {
    if (pressed) g_buttons |=  BTN_FIRE;
    else         g_buttons &= ~BTN_FIRE;
}

EMSCRIPTEN_KEEPALIVE void hal_wasm_tap_at(int x, int y) {
    hal_wasm_set_cursor(x, y);
    g_fire_pulse = 1;
}

/* ════════════════════════════════════════════════════════════════════════════
 * HAL implementation
 * ════════════════════════════════════════════════════════════════════════════ */

void hal_init(void) {
    memset(fb, 0, sizeof(fb));
    g_start_ms = (uint32_t)(emscripten_get_now());

    /* Load background PNG via LodePNG (file is preloaded into WASM FS) */
    unsigned char *raw = NULL;
    unsigned img_w = 0, img_h = 0;
    unsigned err = lodepng_decode32_file_w(&raw, &img_w, &img_h, "/background-1.png");
    if (err == 0 && raw != NULL) {
        /* Scale down to 320×240 using nearest-neighbour.
           The source is 960×720 (exactly 3×), but we handle any size. */
        for (int y = 0; y < SCREEN_H; y++) {
            int src_y = (int)((float)y / SCREEN_H * img_h);
            for (int x = 0; x < SCREEN_W; x++) {
                int src_x = (int)((float)x / SCREEN_W * img_w);
                const unsigned char *px = raw + (src_y * img_w + src_x) * 4;
                uint8_t r = px[0], g = px[1], b = px[2];
                bg_fb[y][x] = RGB565(r, g, b);
            }
        }
        bg_loaded = 1;
        free(raw);
    }

    /* Load civilian-target sprite */
    raw = NULL; img_w = 0; img_h = 0;
    err = lodepng_decode32_file_w(&raw, &img_w, &img_h, "/civilian-target.png");
    if (err == 0 && raw != NULL) {
        for (int y = 0; y < CITY_SPRITE_H; y++) {
            int src_y = (int)((float)y / CITY_SPRITE_H * img_h);
            for (int x = 0; x < CITY_SPRITE_W; x++) {
                int src_x = (int)((float)x / CITY_SPRITE_W * img_w);
                const unsigned char *px = raw + (src_y * img_w + src_x) * 4;
                sprite_data[SPRITE_CITY][y][x][0] = px[0];
                sprite_data[SPRITE_CITY][y][x][1] = px[1];
                sprite_data[SPRITE_CITY][y][x][2] = px[2];
                sprite_data[SPRITE_CITY][y][x][3] = px[3];
            }
        }
        sprite_loaded[SPRITE_CITY] = 1;
        free(raw);
    }

    /* Load bunker sprite */
    raw = NULL; img_w = 0; img_h = 0;
    err = lodepng_decode32_file_w(&raw, &img_w, &img_h, "/bunker-1.png");
    if (err == 0 && raw != NULL) {
        for (int y = 0; y < BUNKER_SPRITE_H; y++) {
            int src_y = (int)((float)y / BUNKER_SPRITE_H * img_h);
            for (int x = 0; x < BUNKER_SPRITE_W; x++) {
                int src_x = (int)((float)x / BUNKER_SPRITE_W * img_w);
                const unsigned char *px = raw + (src_y * img_w + src_x) * 4;
                sprite_data[SPRITE_BUNKER][y][x][0] = px[0];
                sprite_data[SPRITE_BUNKER][y][x][1] = px[1];
                sprite_data[SPRITE_BUNKER][y][x][2] = px[2];
                sprite_data[SPRITE_BUNKER][y][x][3] = px[3];
            }
        }
        sprite_loaded[SPRITE_BUNKER] = 1;
        free(raw);
    }

    js_setup_input();
}

void hal_clear(uint16_t color) {
    if (bg_loaded) {
        memcpy(fb, bg_fb, sizeof(fb));
    } else {
        for (int y = 0; y < SCREEN_H; y++)
            for (int x = 0; x < SCREEN_W; x++)
                fb[y][x] = color;
    }
}

void hal_draw_pixel(int x, int y, uint16_t color) {
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;
    fb[y][x] = color;
}

void hal_draw_rect(int x, int y, int w, int h, uint16_t color) {
    for (int row = y; row < y + h; row++) {
        if (row < 0 || row >= SCREEN_H) continue;
        for (int col = x; col < x + w; col++) {
            if (col < 0 || col >= SCREEN_W) continue;
            fb[row][col] = color;
        }
    }
}

void hal_draw_line(int x0, int y0, int x1, int y1, uint16_t color) {
    /* Bresenham */
    int dx = x1 - x0; if (dx < 0) dx = -dx;
    int dy = y1 - y0; if (dy < 0) dy = -dy;
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    while (1) {
        hal_draw_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void hal_draw_circle(int cx, int cy, int r, uint16_t color) {
    /* Filled circle via scanlines */
    for (int dy = -r; dy <= r; dy++) {
        int half_w = isqrt_approx(r * r - dy * dy);
        hal_draw_rect(cx - half_w, cy + dy, half_w * 2 + 1, 1, color);
    }
}

void hal_draw_circle_top_half(int cx, int cy, int r, uint16_t color) {
    /* Filled semicircle: top half only (dy from -r to 0 from center) */
    for (int dy = -r; dy <= 0; dy++) {
        int half_w = isqrt_approx(r * r - dy * dy);
        hal_draw_rect(cx - half_w, cy + dy, half_w * 2 + 1, 1, color);
    }
}

static uint16_t interpolate_rgb565_wasm(uint16_t color_center, uint16_t color_edge, int ratio) {
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

void hal_draw_circle_top_half_gradient(int cx, int cy, int r, uint16_t color_edge) {
    /* Draw concentric half-circles from white center to color_edge */
    /* Draw from largest to smallest so white center stays on top */
    uint16_t color_white = RGB565(255, 255, 255);
    int steps = r;
    if (steps < 1) steps = 1;
    
    for (int i = steps; i >= 0; i--) {
        int ratio = i * 255 / steps;  /* 255 at edge (i=steps), 0 at center (i=0) */
        uint16_t color = interpolate_rgb565_wasm(color_white, color_edge, ratio);
        int r_i = (int)((long)i * r / steps);
        
        for (int dy = -r_i; dy <= 0; dy++) {
            int half_w = isqrt_approx(r_i * r_i - dy * dy);
            hal_draw_rect(cx - half_w, cy + dy, half_w * 2 + 1, 1, color);
        }
    }
}

/* Local isqrt used only inside this translation unit */
static int isqrt_approx(int n) {
    if (n <= 0) return 0;
    int x = n, y = 1;
    while (x > y) { x = (x + y) / 2; y = n / x; }
    return x;
}

/* ── 5×7 font (printable ASCII starting at 0x20) ─────────────────────────── */
/* Each character is 5 columns × 7 rows, packed 1 bit per row in 5 bytes.    */
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* ' ' */
    {0x00,0x00,0x5F,0x00,0x00}, /* '!' */
    {0x00,0x07,0x00,0x07,0x00}, /* '"' */
    {0x14,0x7F,0x14,0x7F,0x14}, /* '#' */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* '$' */
    {0x23,0x13,0x08,0x64,0x62}, /* '%' */
    {0x36,0x49,0x55,0x22,0x50}, /* '&' */
    {0x00,0x05,0x03,0x00,0x00}, /* ''' */
    {0x00,0x1C,0x22,0x41,0x00}, /* '(' */
    {0x00,0x41,0x22,0x1C,0x00}, /* ')' */
    {0x08,0x2A,0x1C,0x2A,0x08}, /* '*' */
    {0x08,0x08,0x3E,0x08,0x08}, /* '+' */
    {0x00,0x50,0x30,0x00,0x00}, /* ',' */
    {0x08,0x08,0x08,0x08,0x08}, /* '-' */
    {0x00,0x60,0x60,0x00,0x00}, /* '.' */
    {0x20,0x10,0x08,0x04,0x02}, /* '/' */
    {0x3E,0x51,0x49,0x45,0x3E}, /* '0' */
    {0x00,0x42,0x7F,0x40,0x00}, /* '1' */
    {0x42,0x61,0x51,0x49,0x46}, /* '2' */
    {0x21,0x41,0x45,0x4B,0x31}, /* '3' */
    {0x18,0x14,0x12,0x7F,0x10}, /* '4' */
    {0x27,0x45,0x45,0x45,0x39}, /* '5' */
    {0x3C,0x4A,0x49,0x49,0x30}, /* '6' */
    {0x01,0x71,0x09,0x05,0x03}, /* '7' */
    {0x36,0x49,0x49,0x49,0x36}, /* '8' */
    {0x06,0x49,0x49,0x29,0x1E}, /* '9' */
    {0x00,0x36,0x36,0x00,0x00}, /* ':' */
    {0x00,0x56,0x36,0x00,0x00}, /* ';' */
    {0x08,0x14,0x22,0x41,0x00}, /* '<' */
    {0x14,0x14,0x14,0x14,0x14}, /* '=' */
    {0x00,0x41,0x22,0x14,0x08}, /* '>' */
    {0x02,0x01,0x51,0x09,0x06}, /* '?' */
    {0x32,0x49,0x79,0x41,0x3E}, /* '@' */
    {0x7E,0x11,0x11,0x11,0x7E}, /* 'A' */
    {0x7F,0x49,0x49,0x49,0x36}, /* 'B' */
    {0x3E,0x41,0x41,0x41,0x22}, /* 'C' */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 'D' */
    {0x7F,0x49,0x49,0x49,0x41}, /* 'E' */
    {0x7F,0x09,0x09,0x09,0x01}, /* 'F' */
    {0x3E,0x41,0x49,0x49,0x7A}, /* 'G' */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 'H' */
    {0x00,0x41,0x7F,0x41,0x00}, /* 'I' */
    {0x20,0x40,0x41,0x3F,0x01}, /* 'J' */
    {0x7F,0x08,0x14,0x22,0x41}, /* 'K' */
    {0x7F,0x40,0x40,0x40,0x40}, /* 'L' */
    {0x7F,0x02,0x04,0x02,0x7F}, /* 'M' */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 'N' */
    {0x3E,0x41,0x41,0x41,0x3E}, /* 'O' */
    {0x7F,0x09,0x09,0x09,0x06}, /* 'P' */
    {0x3E,0x41,0x51,0x21,0x5E}, /* 'Q' */
    {0x7F,0x09,0x19,0x29,0x46}, /* 'R' */
    {0x46,0x49,0x49,0x49,0x31}, /* 'S' */
    {0x01,0x01,0x7F,0x01,0x01}, /* 'T' */
    {0x3F,0x40,0x40,0x40,0x3F}, /* 'U' */
    {0x1F,0x20,0x40,0x20,0x1F}, /* 'V' */
    {0x3F,0x40,0x38,0x40,0x3F}, /* 'W' */
    {0x63,0x14,0x08,0x14,0x63}, /* 'X' */
    {0x03,0x04,0x78,0x04,0x03}, /* 'Y' */
    {0x61,0x51,0x49,0x45,0x43}, /* 'Z' */
    {0x00,0x7F,0x41,0x41,0x00}, /* '[' */
    {0x02,0x04,0x08,0x10,0x20}, /* '\' */
    {0x00,0x41,0x41,0x7F,0x00}, /* ']' */
    {0x04,0x02,0x01,0x02,0x04}, /* '^' */
    {0x40,0x40,0x40,0x40,0x40}, /* '_' */
    {0x00,0x01,0x02,0x04,0x00}, /* '`' */
    {0x20,0x54,0x54,0x54,0x78}, /* 'a' */
    {0x7F,0x48,0x44,0x44,0x38}, /* 'b' */
    {0x38,0x44,0x44,0x44,0x20}, /* 'c' */
    {0x38,0x44,0x44,0x48,0x7F}, /* 'd' */
    {0x38,0x54,0x54,0x54,0x18}, /* 'e' */
    {0x08,0x7E,0x09,0x01,0x02}, /* 'f' */
    {0x08,0x14,0x54,0x54,0x3C}, /* 'g' */
    {0x7F,0x08,0x04,0x04,0x78}, /* 'h' */
    {0x00,0x44,0x7D,0x40,0x00}, /* 'i' */
    {0x20,0x40,0x44,0x3D,0x00}, /* 'j' */
    {0x7F,0x10,0x28,0x44,0x00}, /* 'k' */
    {0x00,0x41,0x7F,0x40,0x00}, /* 'l' */
    {0x7C,0x04,0x18,0x04,0x78}, /* 'm' */
    {0x7C,0x08,0x04,0x04,0x78}, /* 'n' */
    {0x38,0x44,0x44,0x44,0x38}, /* 'o' */
    {0x7C,0x14,0x14,0x14,0x08}, /* 'p' */
    {0x08,0x14,0x14,0x18,0x7C}, /* 'q' */
    {0x7C,0x08,0x04,0x04,0x08}, /* 'r' */
    {0x48,0x54,0x54,0x54,0x20}, /* 's' */
    {0x04,0x3F,0x44,0x40,0x20}, /* 't' */
    {0x3C,0x40,0x40,0x40,0x7C}, /* 'u' */
    {0x1C,0x20,0x40,0x20,0x1C}, /* 'v' */
    {0x3C,0x40,0x30,0x40,0x3C}, /* 'w' */
    {0x44,0x28,0x10,0x28,0x44}, /* 'x' */
    {0x0C,0x50,0x50,0x50,0x3C}, /* 'y' */
    {0x44,0x64,0x54,0x4C,0x44}, /* 'z' */
    {0x00,0x08,0x36,0x41,0x00}, /* '{' */
    {0x00,0x00,0x7F,0x00,0x00}, /* '|' */
    {0x00,0x41,0x36,0x08,0x00}, /* '}' */
    {0x08,0x08,0x2A,0x1C,0x08}, /* '~' */
};

void hal_draw_char(int x, int y, char c, uint16_t color, uint16_t bg) {
    if (c < 0x20 || c > 0x7E) c = '?';
    const uint8_t *glyph = font5x7[(uint8_t)c - 0x20];
    for (int col = 0; col < 5; col++) {
        uint8_t line = glyph[col];
        for (int row = 0; row < 7; row++) {
            if (line & (1 << row)) {
                hal_draw_pixel(x + col, y + row, color);
            } else if (bg != COL_BLACK) {
                hal_draw_pixel(x + col, y + row, bg);
            }
        }
    }
}

void hal_draw_text(int x, int y, const char *str, uint16_t color, uint16_t bg) {
    while (*str) {
        hal_draw_char(x, y, *str, color, bg);
        x += 6; /* 5px glyph + 1px spacing */
        str++;
    }
}

void hal_draw_text_scaled(int x, int y, const char *str, uint16_t color, int scale) {
    if (scale < 1) scale = 1;
    while (*str) {
        char c = *str;
        if (c < 0x20 || c > 0x7E) c = '?';
        const uint8_t *glyph = font5x7[(uint8_t)c - 0x20];

        for (int col = 0; col < 5; col++) {
            uint8_t line = glyph[col];
            for (int row = 0; row < 7; row++) {
                if (line & (1u << row)) {
                    hal_draw_rect(x + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }

        x += 6 * scale; /* 5px glyph + 1px spacing, both scaled */
        str++;
    }
}

void hal_draw_sprite(int cx, int y_bottom, int sprite_id) {
    if (sprite_id < 0 || sprite_id >= MAX_SPRITES) {
        return;
    }

    const int w = sprite_w[sprite_id];
    const int h = sprite_h[sprite_id];
    y_bottom += sprite_y_offset[sprite_id];

    if (!sprite_loaded[sprite_id]) {
        if (sprite_id == SPRITE_CITY) {
            /* Fallback: city as a blue block. */
            hal_draw_rect(cx - CITY_SPRITE_W / 2, y_bottom - CITY_SPRITE_H,
                          CITY_SPRITE_W, CITY_SPRITE_H, COL_DKBLUE);
        } else {
            /* Fallback: bunker as a green block. */
            hal_draw_rect(cx - BUNKER_SPRITE_W / 2, y_bottom - BUNKER_SPRITE_H,
                          BUNKER_SPRITE_W, BUNKER_SPRITE_H, COL_DKGREEN);
        }
        return;
    }
    int x0 = cx - w / 2;
    int y0 = y_bottom - h;
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            uint8_t a = sprite_data[sprite_id][row][col][3];
            if (a == 0) continue;  /* fully transparent */
            int px = x0 + col, py = y0 + row;
            if (px < 0 || px >= SCREEN_W || py < 0 || py >= SCREEN_H) continue;
            uint8_t sr = sprite_data[sprite_id][row][col][0];
            uint8_t sg = sprite_data[sprite_id][row][col][1];
            uint8_t sb = sprite_data[sprite_id][row][col][2];
            if (a == 255) {
                fb[py][px] = RGB565(sr, sg, sb);
            } else {
                /* Alpha blend over whatever is already in the framebuffer */
                uint16_t dst = fb[py][px];
                uint8_t dr = (dst >> 8) & 0xF8;
                uint8_t dg = (dst >> 3) & 0xFC;
                uint8_t db = (dst << 3) & 0xF8;
                uint8_t inv = 255 - a;
                uint8_t r = (sr * a + dr * inv) >> 8;
                uint8_t g = (sg * a + dg * inv) >> 8;
                uint8_t b = (sb * a + db * inv) >> 8;
                fb[py][px] = RGB565(r, g, b);
            }
        }
    }
}

void hal_draw_ground(void) {
    uint16_t ground = RGB565(GROUND_R, GROUND_G, GROUND_B);
    hal_draw_rect(0, SCREEN_H - GROUND_HEIGHT, SCREEN_W, GROUND_HEIGHT, ground);
}

void hal_play_sound(int sound_id) {
    js_play_sound(sound_id);
}

void hal_present(void) {
    js_blit_framebuffer((const uint16_t *)fb, SCREEN_W, SCREEN_H);
}

uint8_t hal_read_input(void) {
    uint8_t buttons = g_buttons;
    if (g_fire_pulse) {
        buttons |= BTN_FIRE;
        g_fire_pulse = 0;
    }
    return buttons;
}

int hal_read_cursor_x(void) { return g_cursor_x; }
int hal_read_cursor_y(void) { return g_cursor_y; }

uint32_t hal_ticks_ms(void) {
    return (uint32_t)(emscripten_get_now()) - g_start_ms;
}

void hal_delay_ms(uint32_t ms) {
    /* In WebAssembly, avoid blocking the main thread.
       The game loop uses hal_ticks_ms() instead of delays. */
    (void)ms;
}
