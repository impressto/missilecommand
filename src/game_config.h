#ifndef GAME_CONFIG_H
#define GAME_CONFIG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int impact_radius;
    int intercept_radius;
    int player_radius;
    uint16_t impact_color;
    uint16_t intercept_color;
    uint16_t player_color;
} ExplosionConfig;

typedef struct {
    uint8_t lead_r;
    uint8_t lead_g;
    uint8_t lead_b;
    uint8_t tail_r;
    uint8_t tail_g;
    uint8_t tail_b;
    uint16_t tip_color;
    int fade_dist;
    int enemy_render_min_y;

    int player_len;
    uint8_t player_lead_r;
    uint8_t player_lead_g;
    uint8_t player_lead_b;
    uint8_t player_tail_r;
    uint8_t player_tail_g;
    uint8_t player_tail_b;
} TrailConfig;

typedef struct {
    int indicator_size;
    int indicator_y_offset;
    uint16_t indicator_color;
} AmmoConfig;

typedef struct {
    const char *hud_score_label;
    const char *hud_wave_label;
    int hud_score_x;
    int hud_wave_x;

    const char *wave_complete_title;
    const char *wave_complete_bonus_label;
    const char *wave_complete_next_label;

    const char *game_over_title;
    const char *game_over_subtitle;

    uint16_t ground_color;
    uint16_t star_color;
    uint16_t player_missile_main_color;
    uint16_t player_missile_shadow_color;
    uint16_t crosshair_color;
    uint16_t crosshair_center_color;

    uint16_t hud_label_color;
    uint16_t hud_score_value_color;
    uint16_t hud_wave_value_color;

    uint16_t overlay_bg_color;
    uint16_t wave_complete_title_color;
    uint16_t wave_complete_bonus_label_color;
    uint16_t wave_complete_bonus_value_color;
    uint16_t wave_complete_next_color;

    uint16_t game_over_title_color;
    uint16_t game_over_subtitle_color;
} UiConfig;

typedef struct {
    ExplosionConfig explosion;
    TrailConfig trail;
    AmmoConfig ammo;
    UiConfig ui;
} GameConfig;

extern const GameConfig g_game_cfg;

#ifdef __cplusplus
}
#endif

#endif
