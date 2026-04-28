/**
 * config.h — Student-tunable display & gameplay settings
 *            for the WebAssembly build of Missile Command.
 *
 * Change values here and run `make` to see the effect immediately.
 * These settings only affect the browser build; the ESP32 build is unaffected.
 */

#ifndef CONFIG_H
#define CONFIG_H

/* ── Civilian-target sprite ──────────────────────────────────────────────── */
/* Width and height the sprite is scaled to on screen (pixels). */
#define CITY_SPRITE_W       40
#define CITY_SPRITE_H       24

/* Positive = shift sprite downward (partially into the ground).
   Negative = shift upward (float above ground line).              */
#define CITY_SPRITE_Y_OFFSET  18

/* ── Bunker (missile battery) sprite ────────────────────────────────────── */
#define BUNKER_SPRITE_W       30
#define BUNKER_SPRITE_H       20
#define BUNKER_SPRITE_Y_OFFSET  18

/* ── Ground strip ────────────────────────────────────────────────────────── */
/* Height of the coloured ground band at the bottom of the screen (pixels). */
#define GROUND_HEIGHT   10
/* RGB colour of the ground strip (0–255 per channel). */
#define GROUND_R        15
#define GROUND_G        20
#define GROUND_B        25
/* ── Explosions ────────────────────────────────────────────────────────── */
/* Max radius (pixels) of each explosion type. */
#define EXPL_IMPACT_RADIUS   16   /* enemy hits ground or city */
#define EXPL_INTERCEPT_RADIUS 12  /* enemy missile shot down   */
#define EXPL_PLAYER_RADIUS   22   /* player interceptor burst  */
/* Colours as RGB (0–255 per channel). */
/* Default originals: impact=orange, intercept=yellow, player=cyan */
#define EXPL_IMPACT_R    255
#define EXPL_IMPACT_G    100
#define EXPL_IMPACT_B      0

#define EXPL_INTERCEPT_R 255
#define EXPL_INTERCEPT_G 255
#define EXPL_INTERCEPT_B   0

#define EXPL_PLAYER_R     64
#define EXPL_PLAYER_G    255
#define EXPL_PLAYER_B    255

/* ── Ammo count indicator ────────────────────────────────────────────────── */
/* Pixel scale factor for the ammo number drawn above each bunker.
   1 = tiny 5×7 px digits, 2 = 10×14 px digits (recommended), 3 = 15×21, … */
#define AMMO_INDICATOR_SIZE    1
/* Positive = shift number downward; negative = shift upward.
   0 = default position centred on the bunker face.             */
#define AMMO_INDICATOR_Y_OFFSET  6
/* Text colour (RGB 0–255 per channel). */
#define AMMO_INDICATOR_R     255
#define AMMO_INDICATOR_G     220
#define AMMO_INDICATOR_B       0

/* ── UI text ─────────────────────────────────────────────────────────────── */
/* HUD labels */
#define TEXT_HUD_SCORE_LABEL "SCORE:"
#define TEXT_HUD_WAVE_LABEL "WAVE:"

/* ── HUD block positions ──────────────────────────────────────────────────── */
/* Left edge (pixels) of the score label+value block. */
#define HUD_SCORE_X   2
/* Left edge (pixels) of the wave label+value block.
   Default right-aligns it: SCREEN_W minus (label + up to 3 digit chars). */
#define HUD_WAVE_X    (SCREEN_W - 44)

/* Wave-complete overlay text */
#define TEXT_WAVE_COMPLETE_TITLE "WAVE COMPLETE!"
#define TEXT_WAVE_COMPLETE_BONUS_LABEL "Bonus:"
#define TEXT_WAVE_COMPLETE_NEXT_LABEL "Next wave..."

/* Game-over overlay text */
#define TEXT_GAME_OVER_TITLE "GAME  OVER"
/* Game-over subtitle shown under "GAME OVER". */
#define TEXT_GAME_OVER_SUBTITLE "All barracks lost"

/* ── Enemy missile trail ────────────────────────────────────────────────── */
/* Trail colour nearest the missile tip (hot exhaust core). */
#define TRAIL_LEAD_R   255
#define TRAIL_LEAD_G   220
#define TRAIL_LEAD_B   120
/* Trail colour farther behind the missile (cooler exhaust plume). */
#define TRAIL_TAIL_R   0
#define TRAIL_TAIL_G    16
#define TRAIL_TAIL_B     32
/* Tip pixel colour (default: white). */
#define TRAIL_TIP_R 255
#define TRAIL_TIP_G 255
#define TRAIL_TIP_B 255
/* Do not render enemy missiles or trails inside the top border area. */
#define ENEMY_RENDER_MIN_Y 24
/* Pixels from the tip over which the trail fades to nothing.
   Increase for a longer tail; set to 240 to show the full trail undimmed. */
#define TRAIL_FADE_DIST 60

#endif /* CONFIG_H */
