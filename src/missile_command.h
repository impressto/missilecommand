/**
 * missile_command.h — Game logic API
 *
 * The game core is completely platform-agnostic.
 * It only calls functions declared in hal.h.
 */

#ifndef MISSILE_COMMAND_H
#define MISSILE_COMMAND_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Tunable game constants ──────────────────────────────────────────────── */
#define MAX_ENEMY_MISSILES  12
#define MAX_PLAYER_MISSILES  6
#define MAX_EXPLOSIONS      16
#define NUM_CITIES           6
#define NUM_BATTERIES        3
#define TARGET_FPS          30
#define FRAME_MS            (1000u / TARGET_FPS)

/* ── Public API ──────────────────────────────────────────────────────────── */

/** Call once at program start. */
void game_init(void);

/** Enable or disable demo/autoplay mode before calling game_init(). */
void game_set_demo_mode(int enabled);

/**
 * Call every frame from the platform main loop.
 * delta_ms: milliseconds since the previous frame (for physics scaling).
 * Returns 0 while the game is running, non-zero when the player has lost.
 */
int game_update(uint32_t delta_ms);

/** Render the current game state via HAL drawing calls. */
void game_render(void);

#ifdef __cplusplus
}
#endif

#endif /* MISSILE_COMMAND_H */
