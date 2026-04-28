#include "game_config.h"
#include "hal.h"

/*
 * Student-friendly config file:
 * - Edit values here first (colors, text, sizes, radii).
 * - The CFG_* names can also be overridden from build flags when needed.
 * - wasm-compatible names (for example EXPL_* and TEXT_*) are supported.
 */

#ifndef CFG_EXPL_IMPACT_RADIUS
#ifdef EXPL_IMPACT_RADIUS
#define CFG_EXPL_IMPACT_RADIUS EXPL_IMPACT_RADIUS
#else
#define CFG_EXPL_IMPACT_RADIUS 16
#endif
#endif
#ifndef CFG_EXPL_INTERCEPT_RADIUS
#ifdef EXPL_INTERCEPT_RADIUS
#define CFG_EXPL_INTERCEPT_RADIUS EXPL_INTERCEPT_RADIUS
#else
#define CFG_EXPL_INTERCEPT_RADIUS 12
#endif
#endif
#ifndef CFG_EXPL_PLAYER_RADIUS
#ifdef EXPL_PLAYER_RADIUS
#define CFG_EXPL_PLAYER_RADIUS EXPL_PLAYER_RADIUS
#else
#define CFG_EXPL_PLAYER_RADIUS 22
#endif
#endif

#ifndef CFG_EXPL_IMPACT_R
#ifdef EXPL_IMPACT_R
#define CFG_EXPL_IMPACT_R EXPL_IMPACT_R
#define CFG_EXPL_IMPACT_G EXPL_IMPACT_G
#define CFG_EXPL_IMPACT_B EXPL_IMPACT_B
#else
#define CFG_EXPL_IMPACT_R 255
#define CFG_EXPL_IMPACT_G 100
#define CFG_EXPL_IMPACT_B 0
#endif
#endif
#ifndef CFG_EXPL_INTERCEPT_R
#ifdef EXPL_INTERCEPT_R
#define CFG_EXPL_INTERCEPT_R EXPL_INTERCEPT_R
#define CFG_EXPL_INTERCEPT_G EXPL_INTERCEPT_G
#define CFG_EXPL_INTERCEPT_B EXPL_INTERCEPT_B
#else
#define CFG_EXPL_INTERCEPT_R 255
#define CFG_EXPL_INTERCEPT_G 255
#define CFG_EXPL_INTERCEPT_B 0
#endif
#endif
#ifndef CFG_EXPL_PLAYER_R
#ifdef EXPL_PLAYER_R
#define CFG_EXPL_PLAYER_R EXPL_PLAYER_R
#define CFG_EXPL_PLAYER_G EXPL_PLAYER_G
#define CFG_EXPL_PLAYER_B EXPL_PLAYER_B
#else
#define CFG_EXPL_PLAYER_R 0
#define CFG_EXPL_PLAYER_G 255
#define CFG_EXPL_PLAYER_B 255
#endif
#endif

#ifndef CFG_TRAIL_LEAD_R
#ifdef TRAIL_LEAD_R
#define CFG_TRAIL_LEAD_R TRAIL_LEAD_R
#define CFG_TRAIL_LEAD_G TRAIL_LEAD_G
#define CFG_TRAIL_LEAD_B TRAIL_LEAD_B
#elif defined(TRAIL_R)
#define CFG_TRAIL_LEAD_R TRAIL_R
#define CFG_TRAIL_LEAD_G TRAIL_G
#define CFG_TRAIL_LEAD_B TRAIL_B
#else
#define CFG_TRAIL_LEAD_R 255
#define CFG_TRAIL_LEAD_G 120
#define CFG_TRAIL_LEAD_B 0
#endif
#endif
#ifndef CFG_TRAIL_TAIL_R
#ifdef TRAIL_TAIL_R
#define CFG_TRAIL_TAIL_R TRAIL_TAIL_R
#define CFG_TRAIL_TAIL_G TRAIL_TAIL_G
#define CFG_TRAIL_TAIL_B TRAIL_TAIL_B
#else
#define CFG_TRAIL_TAIL_R 255
#define CFG_TRAIL_TAIL_G 32
#define CFG_TRAIL_TAIL_B 0
#endif
#endif
#ifndef CFG_TRAIL_TIP_R
#ifdef TRAIL_TIP_R
#define CFG_TRAIL_TIP_R TRAIL_TIP_R
#define CFG_TRAIL_TIP_G TRAIL_TIP_G
#define CFG_TRAIL_TIP_B TRAIL_TIP_B
#else
#define CFG_TRAIL_TIP_R 255
#define CFG_TRAIL_TIP_G 255
#define CFG_TRAIL_TIP_B 255
#endif
#endif
#ifndef CFG_TRAIL_FADE_DIST
#ifdef TRAIL_FADE_DIST
#define CFG_TRAIL_FADE_DIST TRAIL_FADE_DIST
#else
#define CFG_TRAIL_FADE_DIST 60
#endif
#endif
#ifndef CFG_ENEMY_RENDER_MIN_Y
#ifdef ENEMY_RENDER_MIN_Y
#define CFG_ENEMY_RENDER_MIN_Y ENEMY_RENDER_MIN_Y
#else
#define CFG_ENEMY_RENDER_MIN_Y 0
#endif
#endif

#ifndef CFG_AMMO_INDICATOR_SIZE
#ifdef AMMO_INDICATOR_SIZE
#define CFG_AMMO_INDICATOR_SIZE AMMO_INDICATOR_SIZE
#else
#define CFG_AMMO_INDICATOR_SIZE 2
#endif
#endif
#ifndef CFG_AMMO_INDICATOR_Y_OFFSET
#ifdef AMMO_INDICATOR_Y_OFFSET
#define CFG_AMMO_INDICATOR_Y_OFFSET AMMO_INDICATOR_Y_OFFSET
#else
#define CFG_AMMO_INDICATOR_Y_OFFSET 0
#endif
#endif
#ifndef CFG_AMMO_INDICATOR_R
#ifdef AMMO_INDICATOR_R
#define CFG_AMMO_INDICATOR_R AMMO_INDICATOR_R
#define CFG_AMMO_INDICATOR_G AMMO_INDICATOR_G
#define CFG_AMMO_INDICATOR_B AMMO_INDICATOR_B
#else
#define CFG_AMMO_INDICATOR_R 255
#define CFG_AMMO_INDICATOR_G 220
#define CFG_AMMO_INDICATOR_B 0
#endif
#endif

#ifndef CFG_TEXT_HUD_SCORE_LABEL
#ifdef TEXT_HUD_SCORE_LABEL
#define CFG_TEXT_HUD_SCORE_LABEL TEXT_HUD_SCORE_LABEL
#else
#define CFG_TEXT_HUD_SCORE_LABEL "SCORE:"
#endif
#endif
#ifndef CFG_TEXT_HUD_WAVE_LABEL
#ifdef TEXT_HUD_WAVE_LABEL
#define CFG_TEXT_HUD_WAVE_LABEL TEXT_HUD_WAVE_LABEL
#else
#define CFG_TEXT_HUD_WAVE_LABEL "WAVE:"
#endif
#endif
#ifndef CFG_HUD_SCORE_X
#ifdef HUD_SCORE_X
#define CFG_HUD_SCORE_X HUD_SCORE_X
#else
#define CFG_HUD_SCORE_X 2
#endif
#endif
#ifndef CFG_HUD_WAVE_X
#ifdef HUD_WAVE_X
#define CFG_HUD_WAVE_X HUD_WAVE_X
#else
#define CFG_HUD_WAVE_X (SCREEN_W - 44)
#endif
#endif
#ifndef CFG_TEXT_WAVE_COMPLETE_TITLE
#ifdef TEXT_WAVE_COMPLETE_TITLE
#define CFG_TEXT_WAVE_COMPLETE_TITLE TEXT_WAVE_COMPLETE_TITLE
#else
#define CFG_TEXT_WAVE_COMPLETE_TITLE "WAVE COMPLETE!"
#endif
#endif
#ifndef CFG_TEXT_WAVE_COMPLETE_BONUS_LABEL
#ifdef TEXT_WAVE_COMPLETE_BONUS_LABEL
#define CFG_TEXT_WAVE_COMPLETE_BONUS_LABEL TEXT_WAVE_COMPLETE_BONUS_LABEL
#else
#define CFG_TEXT_WAVE_COMPLETE_BONUS_LABEL "Bonus:"
#endif
#endif
#ifndef CFG_TEXT_WAVE_COMPLETE_NEXT_LABEL
#ifdef TEXT_WAVE_COMPLETE_NEXT_LABEL
#define CFG_TEXT_WAVE_COMPLETE_NEXT_LABEL TEXT_WAVE_COMPLETE_NEXT_LABEL
#else
#define CFG_TEXT_WAVE_COMPLETE_NEXT_LABEL "Next wave..."
#endif
#endif
#ifndef CFG_TEXT_GAME_OVER_TITLE
#ifdef TEXT_GAME_OVER_TITLE
#define CFG_TEXT_GAME_OVER_TITLE TEXT_GAME_OVER_TITLE
#else
#define CFG_TEXT_GAME_OVER_TITLE "GAME  OVER"
#endif
#endif
#ifndef CFG_TEXT_GAME_OVER_SUBTITLE
#ifdef TEXT_GAME_OVER_SUBTITLE
#define CFG_TEXT_GAME_OVER_SUBTITLE TEXT_GAME_OVER_SUBTITLE
#else
#define CFG_TEXT_GAME_OVER_SUBTITLE "All cities lost"
#endif
#endif

#ifndef CFG_GROUND_R
#ifdef GROUND_R
#define CFG_GROUND_R GROUND_R
#define CFG_GROUND_G GROUND_G
#define CFG_GROUND_B GROUND_B
#else
#define CFG_GROUND_R 15
#define CFG_GROUND_G 20
#define CFG_GROUND_B 25
#endif
#endif

const GameConfig g_game_cfg = {
    .explosion = {
        .impact_radius = CFG_EXPL_IMPACT_RADIUS,
        .intercept_radius = CFG_EXPL_INTERCEPT_RADIUS,
        .player_radius = CFG_EXPL_PLAYER_RADIUS,
        .impact_color = RGB565(CFG_EXPL_IMPACT_R, CFG_EXPL_IMPACT_G, CFG_EXPL_IMPACT_B),
        .intercept_color = RGB565(CFG_EXPL_INTERCEPT_R, CFG_EXPL_INTERCEPT_G, CFG_EXPL_INTERCEPT_B),
        .player_color = RGB565(CFG_EXPL_PLAYER_R, CFG_EXPL_PLAYER_G, CFG_EXPL_PLAYER_B),
    },
    .trail = {
        .lead_r = CFG_TRAIL_LEAD_R,
        .lead_g = CFG_TRAIL_LEAD_G,
        .lead_b = CFG_TRAIL_LEAD_B,
        .tail_r = CFG_TRAIL_TAIL_R,
        .tail_g = CFG_TRAIL_TAIL_G,
        .tail_b = CFG_TRAIL_TAIL_B,
        .tip_color = RGB565(CFG_TRAIL_TIP_R, CFG_TRAIL_TIP_G, CFG_TRAIL_TIP_B),
        .fade_dist = CFG_TRAIL_FADE_DIST,
        .enemy_render_min_y = CFG_ENEMY_RENDER_MIN_Y,
    },
    .ammo = {
        .indicator_size = CFG_AMMO_INDICATOR_SIZE,
        .indicator_y_offset = CFG_AMMO_INDICATOR_Y_OFFSET,
        .indicator_color = RGB565(CFG_AMMO_INDICATOR_R, CFG_AMMO_INDICATOR_G, CFG_AMMO_INDICATOR_B),
    },
    .ui = {
        .hud_score_label = CFG_TEXT_HUD_SCORE_LABEL,
        .hud_wave_label = CFG_TEXT_HUD_WAVE_LABEL,
        .hud_score_x = CFG_HUD_SCORE_X,
        .hud_wave_x = CFG_HUD_WAVE_X,
        .wave_complete_title = CFG_TEXT_WAVE_COMPLETE_TITLE,
        .wave_complete_bonus_label = CFG_TEXT_WAVE_COMPLETE_BONUS_LABEL,
        .wave_complete_next_label = CFG_TEXT_WAVE_COMPLETE_NEXT_LABEL,
        .game_over_title = CFG_TEXT_GAME_OVER_TITLE,
        .game_over_subtitle = CFG_TEXT_GAME_OVER_SUBTITLE,
        .ground_color = RGB565(CFG_GROUND_R, CFG_GROUND_G, CFG_GROUND_B),
        .star_color = COL_GRAY,
        .player_missile_main_color = COL_WHITE,
        .player_missile_shadow_color = COL_GRAY,
        .crosshair_color = COL_WHITE,
        .crosshair_center_color = COL_YELLOW,
        .hud_label_color = COL_WHITE,
        .hud_score_value_color = COL_YELLOW,
        .hud_wave_value_color = COL_CYAN,
        .overlay_bg_color = COL_BLACK,
        .wave_complete_title_color = COL_CYAN,
        .wave_complete_bonus_label_color = COL_WHITE,
        .wave_complete_bonus_value_color = COL_YELLOW,
        .wave_complete_next_color = COL_GREEN,
        .game_over_title_color = COL_RED,
        .game_over_subtitle_color = COL_WHITE,
    },
};
