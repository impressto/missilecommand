/**
 * missile_command.c — Core game logic
 *
 * No platform-specific code here.  All rendering goes through hal.h.
 * All math uses integer arithmetic for ESP32 compatibility.
 */

#include "missile_command.h"
#include "hal.h"
#include "game_config.h"

#include <stdint.h>
#include <string.h>   /* memset */
#include <limits.h>

/* ── Integer square-root (no <math.h> needed) ───────────────────────────── */
static int isqrt(int n) {
    if (n <= 0) return 0;
    int x = n, y = 1;
    while (x > y) { x = (x + y) / 2; y = n / x; }
    return x;
}

/* ── Fixed-point helpers (8 fractional bits) ─────────────────────────────── */
#define FP_SHIFT   8
#define FP_ONE     (1 << FP_SHIFT)
#define INT_TO_FP(x)  ((x) << FP_SHIFT)
#define FP_TO_INT(x)  ((x) >> FP_SHIFT)

/* ── Game object structs ─────────────────────────────────────────────────── */

typedef struct {
    int active;
    int x, y;          /* fixed-point position */
    int vx, vy;        /* fixed-point velocity (pixels per second in FP) */
    int tx, ty;        /* integer target pixel */
} Missile;

typedef struct {
    int active;
    int x, y;          /* centre, integer pixels */
    int radius;        /* current explosion radius (integer pixels) */
    int max_radius;
    int growing;       /* 1 = expanding, 0 = shrinking */
    uint16_t color;
} Explosion;

typedef struct {
    int alive;
    int x;             /* centre x, integer pixels */
} City;

typedef struct {
    int x;             /* centre x, integer pixels */
    int ammo;
    int destroyed;     /* 1 if battery itself was destroyed by enemy impact */
} Battery;

/* ── Game state ──────────────────────────────────────────────────────────── */

static struct {
    Missile  enemies[MAX_ENEMY_MISSILES];
    Missile  players[MAX_PLAYER_MISSILES];
    Explosion explosions[MAX_EXPLOSIONS];
    City     cities[NUM_CITIES];
    Battery  batteries[NUM_BATTERIES];

    int      score;
    int      wave;
    int      lives;
    int      game_over;
    int      final_city_flash_active;
    uint32_t final_city_flash_ms;

    /* Wave management */
    int      enemies_to_launch;   /* total enemies for this wave */
    int      enemies_launched;     /* how many already spawned */
    int      wave_complete;        /* 1 = calculating bonuses */
    uint32_t bonus_display_timer;  /* pause to show bonus points */
    int      last_bonus_score;     /* for display */

    /* Difficulty multiplier: M = 1.0 + (wave - 1) * 0.2 */
    /* Store as fixed-point: difficulty_fp = M * 256 */
    int      difficulty_fp;

    /* City rebuild tracking (every 10,000 points) */
    int      last_rebuild_threshold;

    /* Enemy spawn timer (fixed-point ms accumulator) */
    uint32_t spawn_timer_ms;
    uint32_t spawn_interval_ms;  /* decreases each wave */

    /* Cursor position (read from HAL each frame) */
    int      cursor_x;
    int      cursor_y;

    /* Fire button de-bounce */
    uint8_t  prev_buttons;

    /* Demo/autoplay mode */
    int      demo_mode;
    uint32_t demo_fire_cooldown_ms;
} gs;

static int g_demo_mode_selected = 0;

/* ── Ground-level y coordinate ───────────────────────────────────────────── */
#define GROUND_Y        (SCREEN_H - 14)
#define CITY_WIDTH      20
#define CITY_HEIGHT     10
#define BATTERY_WIDTH   16
#define BATTERY_HEIGHT  12
#define BATTERY_AMMO_MAX 10
#define FINAL_CITY_FLASH_DURATION_MS 850u

/* Text and visual tuning values live in game_config.c/game_config.h. */

/* ── Enemy missile trail history (ring buffer, one per missile slot) ─────── */
/* Stores the actual pixel positions flown so the rendered tail follows the
   true trajectory instead of a straight backward projection along velocity.
   Only a new entry is pushed when the integer pixel position changes.        */
#define TRAIL_HISTORY_LEN 64
static int16_t trail_hist_x[MAX_ENEMY_MISSILES][TRAIL_HISTORY_LEN];
static int16_t trail_hist_y[MAX_ENEMY_MISSILES][TRAIL_HISTORY_LEN];
static uint8_t trail_hist_head[MAX_ENEMY_MISSILES];
static uint8_t trail_hist_count[MAX_ENEMY_MISSILES];

/* ── Simple LCG pseudo-random (no stdlib rand needed) ───────────────────── */
static uint32_t rng_state = 12345u;
static uint32_t rng_next(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}
static void rng_seed_runtime(void) {
    uint32_t seed = hal_ticks_ms();
    seed ^= ((uint32_t)hal_read_cursor_x() << 16);
    seed ^= ((uint32_t)hal_read_cursor_y() << 1);
    seed ^= ((uint32_t)hal_read_input() << 24);
    seed ^= 0xA5A55A5Au;
    if (seed == 0u) seed = 12345u;
    rng_state = seed;
    /* Warm up the LCG to decorrelate initial values from the raw seed bits. */
    (void)rng_next();
    (void)rng_next();
}
static int rng_range(int lo, int hi) {
    /* hi is exclusive */
    return lo + (int)((rng_next() >> 16) % (uint32_t)(hi - lo));
}

/* ── Explosion helpers ───────────────────────────────────────────────────── */

static void spawn_explosion(int x, int y, int max_r, uint16_t color) {
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        Explosion *e = &gs.explosions[i];
        if (!e->active) {
            e->active     = 1;
            e->x          = x;
            e->y          = y;
            e->radius     = 1;
            e->max_radius = max_r;
            e->growing    = 1;
            e->color      = color;
            return;
        }
    }
}

/* ── Enemy missile helpers ───────────────────────────────────────────────── */

static void spawn_enemy_missile(void) {
    /* Check if we've launched all enemies for this wave */
    if (gs.enemies_launched >= gs.enemies_to_launch) return;

    /* Find a free slot */
    for (int i = 0; i < MAX_ENEMY_MISSILES; i++) {
        Missile *m = &gs.enemies[i];
        if (!m->active) {
            /* Random start along the top */
            int sx = rng_range(10, SCREEN_W - 10);
            int sy = 0;

            /* Pick a random city or battery as the target
             * NOTE: Enemy can target destroyed cities/batteries (wasteful but allowed) */
            int target_x[NUM_CITIES + NUM_BATTERIES];
            int target_y[NUM_CITIES + NUM_BATTERIES];
            int n = 0;
            
            /* Add all city positions (alive or destroyed) */
            for (int c = 0; c < NUM_CITIES; c++) {
                target_x[n] = gs.cities[c].x;
                target_y[n] = GROUND_Y;
                n++;
            }
            
            /* Add all battery positions (functional or destroyed) */
            for (int b = 0; b < NUM_BATTERIES; b++) {
                target_x[n] = gs.batteries[b].x;
                target_y[n] = GROUND_Y;
                n++;
            }
            
            if (n == 0) return; /* should never happen */

            int pick = rng_range(0, n);
            int tx = target_x[pick];
            int ty = target_y[pick];

            /* Speed scales with difficulty multiplier: Speed = BaseSpeed * M
             * M = 1.0 + (wave - 1) * 0.2
             * difficulty_fp = M * 256 */
            int base_speed_fp = INT_TO_FP(30); /* base 30 pixels/sec */
            int speed_fp = (base_speed_fp * gs.difficulty_fp) / 256;

            int dx = tx - sx;
            int dy = ty - sy;
            int dist = isqrt(dx * dx + dy * dy);
            if (dist == 0) dist = 1;

            m->active = 1;
            m->x  = INT_TO_FP(sx);
            m->y  = INT_TO_FP(sy);
            m->tx = tx;
            m->ty = ty;
            m->vx = (speed_fp * dx) / dist;
            m->vy = (speed_fp * dy) / dist;
            
            gs.enemies_launched++;
            trail_hist_head[i]  = 0;
            trail_hist_count[i] = 0;
            hal_play_sound(SND_ALERT);
            return;
        }
    }
}

/* ── Player fires a missile from the nearest battery with ammo ───────────── */

static int player_fire(void) {
    int cx = gs.cursor_x;
    int cy = gs.cursor_y;

    /* Find nearest battery with ammo AND not destroyed */
    int best = -1, best_dist = 0x7FFFFFFF;
    for (int b = 0; b < NUM_BATTERIES; b++) {
        /* Priority Rule: ignore destroyed batteries or batteries with 0 ammo */
        if (gs.batteries[b].destroyed || gs.batteries[b].ammo <= 0) continue;
        int dx = gs.batteries[b].x - cx;
        int dy = GROUND_Y - cy;
        int d  = dx * dx + dy * dy;
        if (d < best_dist) { best_dist = d; best = b; }
    }
    if (best < 0) return 0; /* no usable battery */

    /* Find a free player missile slot */
    for (int i = 0; i < MAX_PLAYER_MISSILES; i++) {
        Missile *m = &gs.players[i];
        if (!m->active) {
            int sx = gs.batteries[best].x;
            int sy = GROUND_Y;
            int dx = cx - sx;
            int dy = cy - sy;
            int dist = isqrt(dx * dx + dy * dy);
            if (dist == 0) dist = 1;

            int speed_fp = INT_TO_FP(120); /* faster than enemy */

            m->active = 1;
            m->x  = INT_TO_FP(sx);
            m->y  = INT_TO_FP(sy);
            m->tx = cx;
            m->ty = cy;
            m->vx = (speed_fp * dx) / dist;
            m->vy = (speed_fp * dy) / dist;

            hal_play_sound(SND_LAUNCH);
            gs.batteries[best].ammo--;
            return 1;
        }
    }

    return 0;
}

/* ── Check if a point is inside any active explosion ─────────────────────── */

static int point_in_explosion(int x, int y) {
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        Explosion *e = &gs.explosions[i];
        if (!e->active) continue;
        int dx = x - e->x;
        int dy = y - e->y;
        if (dx * dx + dy * dy <= e->radius * e->radius) return 1;
    }
    return 0;
}

static int demo_count_usable_batteries(void) {
    int n = 0;
    for (int b = 0; b < NUM_BATTERIES; b++) {
        if (!gs.batteries[b].destroyed && gs.batteries[b].ammo > 0) n++;
    }
    return n;
}

static int demo_total_ammo(void) {
    int total = 0;
    for (int b = 0; b < NUM_BATTERIES; b++) {
        if (!gs.batteries[b].destroyed) total += gs.batteries[b].ammo;
    }
    return total;
}

static int demo_is_target_already_covered(int x, int y) {
    if (point_in_explosion(x, y)) return 1;

    for (int i = 0; i < MAX_PLAYER_MISSILES; i++) {
        Missile *m = &gs.players[i];
        if (!m->active) continue;
        int dx = m->tx - x;
        int dy = m->ty - y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx <= 16 && dy <= 16) return 1;
    }

    return 0;
}

static int demo_target_threat_score(const Missile *m) {
    int score = 0;
    int py = FP_TO_INT(m->y);

    score += py * 3;

    for (int c = 0; c < NUM_CITIES; c++) {
        if (!gs.cities[c].alive) continue;
        int dx = gs.cities[c].x - m->tx;
        if (dx < 0) dx = -dx;
        if (dx < CITY_WIDTH / 2 + 10) {
            score += 280;
            break;
        }
    }

    for (int b = 0; b < NUM_BATTERIES; b++) {
        if (gs.batteries[b].destroyed) continue;
        int dx = gs.batteries[b].x - m->tx;
        if (dx < 0) dx = -dx;
        if (dx < BATTERY_WIDTH / 2 + 10) {
            score += 180;
            break;
        }
    }

    return score;
}

static int demo_pick_best_battery(int x, int y) {
    int best = -1;
    int best_dist = INT_MAX;
    for (int b = 0; b < NUM_BATTERIES; b++) {
        if (gs.batteries[b].destroyed || gs.batteries[b].ammo <= 0) continue;
        int dx = gs.batteries[b].x - x;
        int dy = GROUND_Y - y;
        int d  = dx * dx + dy * dy;
        if (d < best_dist) {
            best_dist = d;
            best = b;
        }
    }
    return best;
}

static int demo_predict_intercept_point(const Missile *m, int battery_idx,
                                        int *out_x, int *out_y, int *out_timing_err_ms) {
    if (battery_idx < 0 || battery_idx >= NUM_BATTERIES) return 0;

    const int battery_x = gs.batteries[battery_idx].x;
    const int player_speed = 120; /* px/s, matches player missile speed */
    int best_err = INT_MAX;
    int best_x = FP_TO_INT(m->x);
    int best_y = FP_TO_INT(m->y);

    for (int t_ms = 120; t_ms <= 2400; t_ms += 80) {
        int ex = FP_TO_INT(m->x + (m->vx * t_ms) / 1000);
        int ey = FP_TO_INT(m->y + (m->vy * t_ms) / 1000);
        if (ex < 0 || ex >= SCREEN_W) continue;
        if (ey < 6 || ey >= GROUND_Y - 2) continue;

        int dx = ex - battery_x;
        int dy = ey - GROUND_Y;
        int dist = isqrt(dx * dx + dy * dy);
        int player_t_ms = (dist * 1000 + (player_speed / 2)) / player_speed;
        int err = player_t_ms - t_ms;
        if (err < 0) err = -err;

        if (err < best_err) {
            best_err = err;
            best_x = ex;
            best_y = ey;
        }
    }

    if (best_err == INT_MAX) return 0;

    *out_x = best_x;
    *out_y = best_y;
    *out_timing_err_ms = best_err;
    return 1;
}

static int demo_choose_shot_target(int *out_x, int *out_y, int *out_score) {
    int best_score = INT_MIN;
    int found = 0;

    for (int i = 0; i < MAX_ENEMY_MISSILES; i++) {
        Missile *m = &gs.enemies[i];
        if (!m->active) continue;

        int ix = FP_TO_INT(m->x);
        int iy = FP_TO_INT(m->y);
        int timing_err = 0;
        int score = demo_target_threat_score(m);
        int battery_idx = demo_pick_best_battery(ix, iy);
        if (battery_idx < 0) continue;
        if (!demo_predict_intercept_point(m, battery_idx, &ix, &iy, &timing_err)) continue;

        score -= timing_err / 4;
        if (demo_is_target_already_covered(ix, iy)) score -= 220;
        if (iy < 24) score -= 80;
        if (iy > GROUND_Y - 18) score += 120;

        if (score > best_score) {
            best_score = score;
            *out_x = ix;
            *out_y = iy;
            found = 1;
        }
    }

    if (!found) {
        *out_x = SCREEN_W / 2;
        *out_y = SCREEN_H / 3;
        *out_score = INT_MIN;
        return 0;
    }

    if (*out_x < 0) *out_x = 0;
    if (*out_x >= SCREEN_W) *out_x = SCREEN_W - 1;
    if (*out_y < 0) *out_y = 0;
    if (*out_y >= SCREEN_H) *out_y = SCREEN_H - 1;

    *out_score = best_score;
    return 1;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Public API
 * ══════════════════════════════════════════════════════════════════════════ */

/* ── Initialize a new wave ────────────────────────────────────────────────── */

static void start_wave(int wave_num) {
    gs.wave = wave_num;
    gs.enemies_launched = 0;
    gs.wave_complete = 0;
    
    /* Calculate enemies to launch this wave: increases with wave number */
    gs.enemies_to_launch = 8 + (wave_num - 1) * 3; /* 8, 11, 14, 17... */
    if (gs.enemies_to_launch > 40) gs.enemies_to_launch = 40;
    
    /* Calculate difficulty multiplier: M = 1.0 + (wave - 1) * 0.2 */
    /* Store as fixed-point: difficulty_fp = M * 256 */
    gs.difficulty_fp = 256 + (wave_num - 1) * 51; /* 0.2 * 256 = 51.2 ≈ 51 */
    
    /* Spawn interval decreases with wave (minimum 400 ms) */
    gs.spawn_interval_ms = 2500u - (wave_num - 1) * 200u;
    if (gs.spawn_interval_ms < 400u) gs.spawn_interval_ms = 400u;
    
    /* Reset ammo for all non-destroyed batteries */
    for (int b = 0; b < NUM_BATTERIES; b++) {
        if (!gs.batteries[b].destroyed) {
            gs.batteries[b].ammo = BATTERY_AMMO_MAX;
        }
    }
    
    /* If player has at least one city, rebuild destroyed batteries */
    int cities_alive = 0;
    for (int c = 0; c < NUM_CITIES; c++) {
        if (gs.cities[c].alive) cities_alive++;
    }
    if (cities_alive > 0) {
        for (int b = 0; b < NUM_BATTERIES; b++) {
            gs.batteries[b].destroyed = 0;
            gs.batteries[b].ammo = BATTERY_AMMO_MAX;
        }
    }
}

void game_set_demo_mode(int enabled) {
    g_demo_mode_selected = enabled ? 1 : 0;
    gs.demo_mode = g_demo_mode_selected;
}

void game_init(void) {
    memset(&gs, 0, sizeof(gs));
    rng_seed_runtime();

    gs.demo_mode = g_demo_mode_selected;
    gs.demo_fire_cooldown_ms = 0;

    gs.lives             = 3;
    gs.last_rebuild_threshold = 0;
    gs.prev_buttons      = 0xFF;  /* Initialize to all pressed so first frame doesn't fire */

    /* Place cities evenly across the bottom, avoiding battery positions */
    int city_xs[NUM_CITIES] = {30, 75, 120, 200, 245, 290};
    for (int i = 0; i < NUM_CITIES; i++) {
        gs.cities[i].alive = 1;
        gs.cities[i].x     = city_xs[i];
    }

    /* Three batteries: left, centre, right */
    gs.batteries[0].x    = 10;
    gs.batteries[1].x    = SCREEN_W / 2;
    gs.batteries[2].x    = SCREEN_W - 10;
    for (int b = 0; b < NUM_BATTERIES; b++) {
        gs.batteries[b].ammo = BATTERY_AMMO_MAX;
        gs.batteries[b].destroyed = 0;
    }
    
    /* Start wave 1 */
    start_wave(1);
}

int game_update(uint32_t delta_ms) {
    if (gs.game_over) return 1;

    if (gs.final_city_flash_active) {
        if (delta_ms >= gs.final_city_flash_ms) {
            gs.final_city_flash_ms = 0;
            gs.final_city_flash_active = 0;
            gs.game_over = 1;
            return 1;
        }
        gs.final_city_flash_ms -= delta_ms;
        return 0;
    }

    /* ── Read input ─────────────────────────────────────────────────────── */
    gs.cursor_x = hal_read_cursor_x();
    gs.cursor_y = hal_read_cursor_y();
    uint8_t buttons = hal_read_input();

    if (gs.demo_mode) {
        int tx = gs.cursor_x;
        int ty = gs.cursor_y;
        int shot_score = INT_MIN;
        int has_target = demo_choose_shot_target(&tx, &ty, &shot_score);
        gs.cursor_x = tx;
        gs.cursor_y = ty;

        if (gs.demo_fire_cooldown_ms > delta_ms) {
            gs.demo_fire_cooldown_ms -= delta_ms;
        } else {
            gs.demo_fire_cooldown_ms = 0;
        }

        if (gs.demo_fire_cooldown_ms == 0 && has_target) {
            int usable_batteries = demo_count_usable_batteries();
            int ammo_total = demo_total_ammo();

            int should_fire = 1;
            if (usable_batteries <= 0 || ammo_total <= 0) {
                should_fire = 0;
            }

            if (shot_score < 90) should_fire = 0;
            if (ammo_total < 3 && shot_score < 260) should_fire = 0;

            if (ty < 26 && ammo_total < 12) {
                should_fire = 0;
            }

            if (should_fire) {
                int fired = player_fire();
                if (fired) {
                    uint32_t base_cd = (ammo_total < 8) ? 380u : 260u;
                    if (shot_score > 320) base_cd = 180u;
                    gs.demo_fire_cooldown_ms = base_cd + (uint32_t)rng_range(0, 120);
                } else {
                    gs.demo_fire_cooldown_ms = 140u;
                }
            } else {
                gs.demo_fire_cooldown_ms = 120u;
            }
        }
    }

    /* Fire on leading edge of FIRE button */
    if (!gs.demo_mode && (buttons & BTN_FIRE) && !(gs.prev_buttons & BTN_FIRE)) {
        player_fire();
    }
    gs.prev_buttons = buttons;

    /* ── Spawn enemy missiles (only if wave not complete) ────────────────── */
    if (!gs.wave_complete) {
        gs.spawn_timer_ms += delta_ms;
        if (gs.spawn_timer_ms >= gs.spawn_interval_ms) {
            gs.spawn_timer_ms = 0;
            spawn_enemy_missile();
            /* Occasionally launch a second one on higher waves */
            if (gs.wave >= 3 && (rng_next() & 1)) spawn_enemy_missile();
        }
    }

    /* ── Move enemy missiles ─────────────────────────────────────────────── */
    for (int i = 0; i < MAX_ENEMY_MISSILES; i++) {
        Missile *m = &gs.enemies[i];
        if (!m->active) continue;

        m->x += (m->vx * (int)delta_ms) / 1000;
        m->y += (m->vy * (int)delta_ms) / 1000;

        int px = FP_TO_INT(m->x);
        int py = FP_TO_INT(m->y);

        /* Record position in trail history (only when pixel actually moves) */
        {
            uint8_t h = trail_hist_head[i];
            if (trail_hist_count[i] == 0 ||
                trail_hist_x[i][h] != (int16_t)px ||
                trail_hist_y[i][h] != (int16_t)py) {
                uint8_t nh = (uint8_t)(((int)h + 1) % TRAIL_HISTORY_LEN);
                trail_hist_x[i][nh] = (int16_t)px;
                trail_hist_y[i][nh] = (int16_t)py;
                trail_hist_head[i]  = nh;
                if (trail_hist_count[i] < TRAIL_HISTORY_LEN) trail_hist_count[i]++;
            }
        }

        /* Reached target or hit the ground */
        if (py >= m->ty || py >= GROUND_Y) {
            spawn_explosion(px, GROUND_Y,
                            g_game_cfg.explosion.impact_radius,
                            g_game_cfg.explosion.impact_color);
            m->active = 0;
            hal_play_sound(SND_IMPACT);

            /* Destroy city/battery at impact */
            for (int c = 0; c < NUM_CITIES; c++) {
                if (!gs.cities[c].alive) continue;
                int dx = gs.cities[c].x - px;
                if (dx < 0) dx = -dx;
                if (dx < CITY_WIDTH / 2 + 8) {
                    gs.cities[c].alive = 0;
                }
            }
            
            /* Check if any battery was hit */
            for (int b = 0; b < NUM_BATTERIES; b++) {
                if (gs.batteries[b].destroyed) continue;
                int dx = gs.batteries[b].x - px;
                if (dx < 0) dx = -dx;
                if (dx < BATTERY_WIDTH / 2 + 8) {
                    gs.batteries[b].destroyed = 1;
                    gs.batteries[b].ammo = 0;
                }
            }
        }

        /* Intercepted by player explosion */
        if (m->active && point_in_explosion(FP_TO_INT(m->x), FP_TO_INT(m->y))) {
            spawn_explosion(FP_TO_INT(m->x), FP_TO_INT(m->y),
                            g_game_cfg.explosion.intercept_radius,
                            g_game_cfg.explosion.intercept_color);
            m->active = 0;
            hal_play_sound(SND_INTERCEPT);
            
            /* Scoring: Base points increase with wave */
            int base_points = 25;
            gs.score += base_points * gs.wave;
        }
    }

    /* ── Move player missiles ────────────────────────────────────────────── */
    for (int i = 0; i < MAX_PLAYER_MISSILES; i++) {
        Missile *m = &gs.players[i];
        if (!m->active) continue;

        m->x += (m->vx * (int)delta_ms) / 1000;
        m->y += (m->vy * (int)delta_ms) / 1000;

        int px = FP_TO_INT(m->x);
        int py = FP_TO_INT(m->y);

        /* Check if close enough to target */
        int dx = px - m->tx;
        int dy = py - m->ty;
        if (dx * dx + dy * dy < 4 * 4) {
            spawn_explosion(m->tx, m->ty,
                            g_game_cfg.explosion.player_radius,
                            g_game_cfg.explosion.player_color);
            m->active = 0;
            hal_play_sound(SND_PLAYER_BURST);
        }
    }

    /* ── Animate explosions ──────────────────────────────────────────────── */
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        Explosion *e = &gs.explosions[i];
        if (!e->active) continue;

        int growth = (int)((20u * delta_ms) / 1000u) + 1; /* ~20 px/sec */

        if (e->growing) {
            e->radius += growth;
            if (e->radius >= e->max_radius) { e->radius = e->max_radius; e->growing = 0; }
        } else {
            e->radius -= growth;
            if (e->radius <= 0) e->active = 0;
        }
    }

    /* ── Wave complete logic ─────────────────────────────────────────────── */
    if (!gs.wave_complete) {
        /* Check if all enemies launched and all destroyed/landed */
        if (gs.enemies_launched >= gs.enemies_to_launch) {
            int any_enemy = 0;
            for (int i = 0; i < MAX_ENEMY_MISSILES; i++) {
                if (gs.enemies[i].active) { any_enemy = 1; break; }
            }
            
            /* Wave complete: calculate bonuses */
            if (!any_enemy) {
                gs.wave_complete = 1;
                gs.bonus_display_timer = 0;
                
                /* Calculate end-of-wave bonus:
                 * Points = (RemainingCities × 100) + (UnusedAmmo × 5) */
                int cities_remaining = 0;
                for (int c = 0; c < NUM_CITIES; c++) {
                    if (gs.cities[c].alive) cities_remaining++;
                }
                
                int ammo_remaining = 0;
                for (int b = 0; b < NUM_BATTERIES; b++) {
                    if (!gs.batteries[b].destroyed) {
                        ammo_remaining += gs.batteries[b].ammo;
                    }
                }
                
                int bonus = (cities_remaining * 100) + (ammo_remaining * 5);
                gs.last_bonus_score = bonus;
                gs.score += bonus;
                
                hal_play_sound(SND_WAVE_COMPLETE);
            }
        }
    } else {
        /* Display bonus and transition to next wave */
        gs.bonus_display_timer += delta_ms;
        if (gs.bonus_display_timer >= 3000) { /* 3 second pause */
            start_wave(gs.wave + 1);
        }
    }
    
    /* ── City rebuild every 10,000 points ────────────────────────────────── */
    {
        int current_threshold = gs.score / 10000;
        if (current_threshold > gs.last_rebuild_threshold) {
            gs.last_rebuild_threshold = current_threshold;
            
            /* Find first destroyed city and rebuild it */
            for (int c = 0; c < NUM_CITIES; c++) {
                if (!gs.cities[c].alive) {
                    gs.cities[c].alive = 1;
                    hal_play_sound(SND_WAVE_COMPLETE); /* reuse this sound */
                    break;
                }
            }
        }
    }

    /* ── Game over: all cities destroyed ─────────────────────────────────── */
    {
        int alive = 0;
        for (int c = 0; c < NUM_CITIES; c++)
            if (gs.cities[c].alive) alive++;
        if (alive == 0) {
            hal_play_sound(SND_GAME_OVER);
            gs.final_city_flash_active = 1;
            gs.final_city_flash_ms = FINAL_CITY_FLASH_DURATION_MS;
            return 0;
        }
    }

    return 0;
}

static uint8_t final_city_fade_amount(uint32_t ms_left) {
    uint32_t d = FINAL_CITY_FLASH_DURATION_MS;
    if (ms_left >= d) return 0;

    uint32_t elapsed = d - ms_left;
    uint32_t a = (224u * elapsed * elapsed) / (d * d); /* smooth quadratic ramp-in */
    if (a > 255u) a = 255u;
    return (uint8_t)a;
}

void game_render(void) {
    /* ── Background ──────────────────────────────────────────────────────── */
    hal_clear(COL_BLACK);

    /* ── Stars (deterministic, based on position) ────────────────────────── */
    /* A handful of fixed star positions to avoid rand() per frame */
    static const uint16_t star_x[] = {15, 42, 87, 130, 175, 210, 260, 300, 55, 185, 240};
    static const uint8_t  star_y[] = {8,  20,  5,  35,  12,  50,  18,  40, 60,  30,   7};
    for (int s = 0; s < (int)(sizeof(star_x) / sizeof(star_x[0])); s++) {
        hal_draw_pixel(star_x[s], star_y[s], g_game_cfg.ui.star_color);
    }

    /* ── Ground ──────────────────────────────────────────────────────────── */
    hal_draw_ground();

    /* ── Cities ──────────────────────────────────────────────────────────── */
    for (int c = 0; c < NUM_CITIES; c++) {
        if (!gs.cities[c].alive) continue;
        int cx = gs.cities[c].x;
        hal_draw_sprite(cx, GROUND_Y, SPRITE_CITY);
    }

    /* ── Batteries ───────────────────────────────────────────────────────── */
    for (int b = 0; b < NUM_BATTERIES; b++) {
        if (gs.batteries[b].destroyed) continue; /* don't draw destroyed batteries */
        int bx = gs.batteries[b].x;
        hal_draw_sprite(bx, GROUND_Y, SPRITE_BUNKER);
        /* Ammo count number above the bunker */
        {
            char buf[4];
            int  n    = 0;
            int  ammo = gs.batteries[b].ammo;
            if (ammo <= 0) {
                buf[n++] = '0';
            } else {
                int tmp = ammo;
                while (tmp > 0) { buf[n++] = '0' + (tmp % 10); tmp /= 10; }
                /* reverse digits into correct order */
                for (int l = 0, r = n - 1; l < r; l++, r--) {
                    char t = buf[l]; buf[l] = buf[r]; buf[r] = t;
                }
            }
            buf[n] = '\0';
            int s  = g_game_cfg.ammo.indicator_size;
            int tw = (6 * n - 1) * s;   /* total pixel width of the string */
            int tx = bx - tw / 2;
            /* Base: centre the digit height on the visible bunker face.
                    Positive indicator_y_offset shifts the number down,
               negative shifts it up.                                    */
                int ty = GROUND_Y - (7 * s) / 2 + g_game_cfg.ammo.indicator_y_offset;
                hal_draw_text_scaled(tx, ty, buf, g_game_cfg.ammo.indicator_color, s);
        }
    }

    /* ── Enemy missiles ──────────────────────────────────────────────────── */
    for (int i = 0; i < MAX_ENEMY_MISSILES; i++) {
        Missile *m = &gs.enemies[i];
        if (!m->active) continue;
        int px = FP_TO_INT(m->x);
        int py = FP_TO_INT(m->y);
        
        /* Draw curved trail by walking the position-history ring buffer */
        {
            int blend_den = g_game_cfg.trail.fade_dist > 1 ? g_game_cfg.trail.fade_dist - 1 : 1;
            int dist = 0;
            int prev_hx = px, prev_hy = py;
            int count = (int)trail_hist_count[i];
            int head  = (int)trail_hist_head[i];
            for (int j = 0; j < count; j++) {
                int idx = (head - j + TRAIL_HISTORY_LEN) % TRAIL_HISTORY_LEN;
                int hx = (int)trail_hist_x[i][idx];
                int hy = (int)trail_hist_y[i][idx];
                int ddx = prev_hx - hx; if (ddx < 0) ddx = -ddx;
                int ddy = prev_hy - hy; if (ddy < 0) ddy = -ddy;
                dist += ddx > ddy ? ddx : ddy; /* Chebyshev step distance */
                if (dist >= g_game_cfg.trail.fade_dist) break;
                if (hy >= g_game_cfg.trail.enemy_render_min_y) {
                    int blend = (dist * 255) / blend_den;
                    uint8_t r = (uint8_t)((g_game_cfg.trail.lead_r * (255 - blend) + g_game_cfg.trail.tail_r * blend) / 255);
                    uint8_t g = (uint8_t)((g_game_cfg.trail.lead_g * (255 - blend) + g_game_cfg.trail.tail_g * blend) / 255);
                    uint8_t b = (uint8_t)((g_game_cfg.trail.lead_b * (255 - blend) + g_game_cfg.trail.tail_b * blend) / 255);
                    hal_draw_pixel(hx, hy, RGB565(r, g, b));
                }
                prev_hx = hx;
                prev_hy = hy;
            }
        }
        if (py >= g_game_cfg.trail.enemy_render_min_y) {
            hal_draw_pixel(px, py, g_game_cfg.trail.tip_color);
        }
    }

    /* ── Player missiles ─────────────────────────────────────────────────── */
    for (int i = 0; i < MAX_PLAYER_MISSILES; i++) {
        Missile *m = &gs.players[i];
        if (!m->active) continue;
        int px = FP_TO_INT(m->x);
        int py = FP_TO_INT(m->y);
        hal_draw_pixel(px,     py,     g_game_cfg.ui.player_missile_main_color);
        hal_draw_pixel(px + 1, py,     g_game_cfg.ui.player_missile_shadow_color);
        hal_draw_pixel(px,     py + 1, g_game_cfg.ui.player_missile_shadow_color);
    }

    /* ── Explosions ──────────────────────────────────────────────────────── */
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        Explosion *e = &gs.explosions[i];
        if (!e->active) continue;
        /* Ground explosions: draw with gradient (white center fading to orange edge) */
        if (e->y == GROUND_Y) {
            hal_draw_circle_top_half_gradient(e->x, e->y, e->radius, e->color);
        } else {
            /* Air explosions (intercepts): draw with gradient (white center to edge color) */
            hal_draw_circle_gradient(e->x, e->y, e->radius, e->color);
        }
    }

    /* ── Crosshair cursor ────────────────────────────────────────────────── */
    {
        int cx = gs.cursor_x;
        int cy = gs.cursor_y;
        hal_draw_line(cx - 6, cy, cx - 2, cy, g_game_cfg.ui.crosshair_color);
        hal_draw_line(cx + 2, cy, cx + 6, cy, g_game_cfg.ui.crosshair_color);
        hal_draw_line(cx, cy - 6, cx, cy - 2, g_game_cfg.ui.crosshair_color);
        hal_draw_line(cx, cy + 2, cx, cy + 6, g_game_cfg.ui.crosshair_color);
        hal_draw_pixel(cx, cy, g_game_cfg.ui.crosshair_center_color);
    }

    /* ── HUD ─────────────────────────────────────────────────────────────── */
    {
        /* Score */
        char buf[32];
        /* Simple int-to-string (no sprintf to avoid heavy libc on ESP32) */
        int s  = gs.score;
        int i  = 30;
        buf[i] = '\0';
        if (s == 0) { buf[--i] = '0'; }
        else { while (s > 0) { buf[--i] = '0' + (s % 10); s /= 10; } }
        {
            int label_w = (int)strlen(g_game_cfg.ui.hud_score_label) * 6;
            hal_draw_text(g_game_cfg.ui.hud_score_x, 2, g_game_cfg.ui.hud_score_label, g_game_cfg.ui.hud_label_color, COL_BLACK);
            hal_draw_text(g_game_cfg.ui.hud_score_x + label_w, 2, &buf[i], g_game_cfg.ui.hud_score_value_color, COL_BLACK);
        }

        /* Wave */
        i = 30; buf[i] = '\0';
        int w = gs.wave;
        while (w > 0) { buf[--i] = '0' + (w % 10); w /= 10; }
        {
            int label_w = (int)strlen(g_game_cfg.ui.hud_wave_label) * 6;
            hal_draw_text(g_game_cfg.ui.hud_wave_x, 2, g_game_cfg.ui.hud_wave_label, g_game_cfg.ui.hud_label_color, COL_BLACK);
            hal_draw_text(g_game_cfg.ui.hud_wave_x + label_w, 2, &buf[i], g_game_cfg.ui.hud_wave_value_color, COL_BLACK);
        }
    }

    if (gs.final_city_flash_active) {
        hal_fade_to_black(final_city_fade_amount(gs.final_city_flash_ms));
    }

    /* ── Wave complete overlay ───────────────────────────────────────────── */
    if (gs.wave_complete && !gs.game_over) {
        hal_draw_rect(60, 80, 200, 60, g_game_cfg.ui.overlay_bg_color);

        int title_len = (int)strlen(g_game_cfg.ui.wave_complete_title);
        int title_w = (title_len > 0) ? (6 * title_len - 1) : 0;
        int title_x = (SCREEN_W - title_w) / 2;
        hal_draw_text(title_x, 88, g_game_cfg.ui.wave_complete_title, g_game_cfg.ui.wave_complete_title_color, COL_BLACK);
        
        /* Display bonus */
        char buf[32];
        int i = 30;
        buf[i] = '\0';
        int bonus = gs.last_bonus_score;
        if (bonus == 0) { buf[--i] = '0'; }
        else { while (bonus > 0) { buf[--i] = '0' + (bonus % 10); bonus /= 10; } }

        int bonus_label_len = (int)strlen(g_game_cfg.ui.wave_complete_bonus_label);
        int bonus_value_len = (int)strlen(&buf[i]);
        int bonus_label_w = (bonus_label_len > 0) ? (6 * bonus_label_len - 1) : 0;
        int bonus_value_w = (bonus_value_len > 0) ? (6 * bonus_value_len - 1) : 0;
        int bonus_gap = 6; /* one character cell gap */
        int bonus_row_w = bonus_label_w + bonus_gap + bonus_value_w;
        int bonus_x = (SCREEN_W - bonus_row_w) / 2;
        hal_draw_text(bonus_x, 102, g_game_cfg.ui.wave_complete_bonus_label, g_game_cfg.ui.wave_complete_bonus_label_color, COL_BLACK);
        hal_draw_text(bonus_x + bonus_label_w + bonus_gap, 102, &buf[i], g_game_cfg.ui.wave_complete_bonus_value_color, COL_BLACK);
        
        int next_len = (int)strlen(g_game_cfg.ui.wave_complete_next_label);
        int next_w = (next_len > 0) ? (6 * next_len - 1) : 0;
        int next_x = (SCREEN_W - next_w) / 2;
        hal_draw_text(next_x, 116, g_game_cfg.ui.wave_complete_next_label, g_game_cfg.ui.wave_complete_next_color, COL_BLACK);
    }
    
    /* ── Game over overlay ───────────────────────────────────────────────── */
    if (gs.game_over) {
        hal_draw_rect(80, 100, 160, 40, g_game_cfg.ui.overlay_bg_color);

        int title_len = (int)strlen(g_game_cfg.ui.game_over_title);
        int subtitle_len = (int)strlen(g_game_cfg.ui.game_over_subtitle);
        int title_w = (title_len > 0) ? (6 * title_len - 1) : 0;
        int subtitle_w = (subtitle_len > 0) ? (6 * subtitle_len - 1) : 0;
        int title_x = (SCREEN_W - title_w) / 2;
        int subtitle_x = (SCREEN_W - subtitle_w) / 2;

        hal_draw_text(title_x, 108, g_game_cfg.ui.game_over_title, g_game_cfg.ui.game_over_title_color, COL_BLACK);
        hal_draw_text(subtitle_x, 122, g_game_cfg.ui.game_over_subtitle, g_game_cfg.ui.game_over_subtitle_color, COL_BLACK);
    }

    hal_present();
}
