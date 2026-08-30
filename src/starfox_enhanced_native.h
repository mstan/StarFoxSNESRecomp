#ifndef STARFOX_ENHANCED_NATIVE_H
#define STARFOX_ENHANCED_NATIVE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct StarFoxEnhancedNativeShapePose {
  double x;
  double y;
  double z;
  uint16_t pitch;
  uint16_t yaw;
  uint16_t roll;
  uint16_t source_pitch;
  uint16_t source_yaw;
  uint16_t source_roll;
  uint16_t previous_source_pitch;
  uint16_t previous_source_yaw;
  uint16_t previous_source_roll;
  int16_t vanish_x;
  int16_t vanish_y;
  uint16_t colour_pointer;
  uint16_t depth_colours;
  uint16_t depth_thresholds;
  double source_depth_z;
  uint8_t object_depth_offset;
  uint8_t explosion_progress;
  uint8_t use_source_view_matrix;
  uint8_t use_source_depth_z;
  uint8_t use_interpolated_object_matrix;
  uint16_t object_matrix_alpha_q8;
  uint8_t use_shadow_shape;
  uint8_t flatten_shadow_matrix;
  uint8_t force_colour;
  uint8_t forced_colour;
  uint8_t simple_scaled_sprite;
  uint8_t simple_sprite_colour;
  int8_t texture_scroll_x;
  int8_t texture_scroll_y;
  uint32_t animation_frame;
  uint32_t colour_frame;
  int16_t source_view_matrix[9];
  uint16_t widescreen_extra;
} StarFoxEnhancedNativeShapePose;

typedef struct StarFoxEnhancedNativeShapeStats {
  unsigned visible_pixels;
  unsigned decode_failures;
  unsigned decoded_vertices;
  unsigned decoded_faces;
  uint16_t selected_lod;
} StarFoxEnhancedNativeShapeStats;

int StarFoxEnhancedDrawNativePpuLayers(uint8_t *pixels, size_t pitch, int width,
                                       int height, uint16_t widescreen_extra,
                                       int suppress_superfx_world_bg1,
                                       int anchor_edge_hud);
unsigned StarFoxEnhancedDrawGameplayHudSprites(uint8_t *pixels, size_t pitch,
                                               int width, int height,
                                               uint16_t widescreen_extra);
unsigned StarFoxEnhancedDrawGameplayHudMeters(
    uint8_t *pixels, size_t pitch, int width, int height,
    uint16_t widescreen_extra, uint8_t damage, uint8_t boost, int shield_up,
    int enabled, uint8_t boss_health, uint8_t boss_max_health);
int StarFoxEnhancedDrawNativeShape(uint8_t *pixels, size_t pitch, int width,
                                   int height, const uint8_t *rom,
                                   size_t rom_size, uint16_t shape_address,
                                   const StarFoxEnhancedNativeShapePose *pose,
                                   StarFoxEnhancedNativeShapeStats *stats);
unsigned StarFoxEnhancedDrawProjectedText(uint8_t *pixels, size_t pitch,
                                          int width, int height,
                                          const uint8_t *rom, size_t rom_size,
                                          uint16_t message_pointer,
                                          uint8_t colour,
                                          int8_t size_adjustment,
                                          const StarFoxEnhancedNativeShapePose *pose);
unsigned StarFoxEnhancedDrawCockpitHud(
    uint8_t *pixels, size_t pitch, int width, int height, const uint8_t *rom,
    size_t rom_size, uint8_t rotation, uint8_t colour, uint8_t damage_flags,
    int horizontal_origin, int vertical_origin,
    uint8_t normal_colour_override);
unsigned StarFoxEnhancedDrawCommsHud(uint8_t *pixels, size_t pitch, int width,
                                     int height, const uint8_t *rom,
                                     size_t rom_size, uint8_t open_count,
                                     uint8_t animation_count,
                                     uint8_t friend_id,
                                     uint16_t face_pointer,
                                     uint32_t text_address);
void StarFoxEnhancedInterpolateMatrixQ15(const int16_t previous[9],
                                         const int16_t current[9],
                                         uint16_t alpha_q8, int16_t out[9]);

#ifdef __cplusplus
}
#endif

#endif
