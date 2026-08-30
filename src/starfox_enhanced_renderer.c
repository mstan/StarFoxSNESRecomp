#include "starfox_enhanced_renderer.h"

#include "common_rtl.h"
#include "config.h"
#include "debug_server.h"
#include "snes/cart.h"
#include "snes/ppu.h"
#include "snes/snes.h"
#include "snes/superfx.h"
#include "starfox_enhanced_native.h"

#include "stb_image_write.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

enum {
  kRamAllst = 0x121d,
  kRamAlFreeLst = 0x121f,
  kRamViewPosX = 0x00c1,
  kRamViewPosY = 0x00c3,
  kRamViewPosZ = 0x00c5,
  kRamVanishX = 0x00ca,
  kRamVanishY = 0x00cc,
  kRamDepthTabPtr = 0x1259,
  kRamMat11W = 0x15d7,
  kRamWmat11W = 0x161b,
  kRamGameFrame = 0x15bb,
  kRamHudRotation = 0x154e,
  kRamPlayerFlyMode = 0x1565,
  kRamShieldUp = 0x1752,
  kRamWhichFriend = 0x191f,
  kRamFriendsMsg = 0x1920,
  kRamMsgCount1 = 0x1922,
  kRamMsgCount2 = 0x1923,
  kRamShadowHeight = 0x19dc,
  kGsuFacePtr = 0x0018,
  kGsuVanishX = 0x0034,
  kGsuVanishY = 0x0036,
  kGsuDepthColours = 0x004e,
  kGsuDepthThresholds = 0x0050,
  kGsuPlayerFlyMode = 0x0174,
  kGsuBossMaxHp = 0x016e,
  kGsuBossHp = 0x0170,
  kGsuDamage = 0x018c,
  kGsuBoostAnim = 0x018e,
  kGsuShieldUp = 0x0190,
  kGsuMeters = 0x0200,
  kGsuShadowHeight = 0x0204,
  kGsuHudColour = 0x3512,
  kGsuHudDamageFlags = 0x3514,
  kObjBase = 0x0336,
  kObjSize = 0x36,
  kObjPoolCount = 0x46,
  kObjNext = 0x00,
  kObjShape = 0x04,
  kObjFlags = 0x08,
  kObjType = 0x09,
  kObjCounter = 0x0a,
  kObjWorldX = 0x0c,
  kObjWorldY = 0x0e,
  kObjWorldZ = 0x10,
  kObjRotX = 0x12,
  kObjRotY = 0x13,
  kObjRotZ = 0x14,
  kObjSFlags = 0x1d,
  kObjSFlags2 = 0x1e,
  kObjSFlags3 = 0x1f,
  kObjSFlags4 = 0x20,
  kObjAuxDepthOffset = 0x1cdf,
  kObjAuxColourFrame = 0x1ce6,
  kObjAuxAnimationFrame = 0x1ce7,
  kObjAuxColourTable = 0x1cea,
  kObjAuxTextureScrollX = 0x1cf4,
  kObjAuxTextureScrollY = 0x1cf5,
  kRomColourWhite = 0x800c,
  kRomColourRed = 0x80fc,
  kRomColourSpecial = 0x82ed,
  kRomMessagesBank = 0x010000,
  kRomDepthThresholdDefault = 0x8faa,
  kShapeNull = 0xaca1,
  kAfExplosion = 0x01,
  kAtGround = 0x01,
  kPfmShadows = 0x08,
  kSourceVanishDefaultX = 112,
  kSourceVanishDefaultY = 96,
  kSuperFxHorizontalInset = 16,
  kSuperFxVerticalInset = 16,
  kAsfShadowShape = 0x04,
  kAsfShadow = 0x08,
  kAsfShadowMask = kAsfShadowShape | kAsfShadow,
  kAsfPartObj = 0x10,
  kAsfScaledSprite = 0x20,
  kAsfTextObj = 0x40,
  kAsfInvisible4 = 0x08,
  kShadowForcedColour = 0x09,
};

typedef struct NativeSourceObject {
  uint8_t handle;
  uint16_t pointer;
  uint16_t shape;
  uint16_t colour_pointer;
  uint16_t fire_object;
  int16_t world_x;
  int16_t world_y;
  int16_t world_z;
  int16_t camera_x;
  int16_t camera_y;
  int16_t camera_z;
  int16_t sort_depth;
  uint8_t pitch;
  uint8_t yaw;
  uint8_t roll;
  uint8_t flags;
  uint8_t type;
  uint8_t count;
  uint8_t sflags[4];
  uint8_t strategy_state;
  uint8_t animation_frame;
  uint8_t colour_frame;
  uint8_t object_depth_offset;
  uint8_t metadata_valid;
  uint8_t culled;
  uint8_t explosion_count;
  uint8_t sound1;
  uint8_t sound2;
  int8_t texture_scroll_x;
  int8_t texture_scroll_y;
} NativeSourceObject;

typedef struct NativeSourceFrameSnapshot {
  int valid;
  int frame;
  uint8_t game_frame;
  int16_t view_x;
  int16_t view_y;
  int16_t view_z;
  int16_t raw_view_z;
  int16_t view_matrix[9];
  int16_t vanish_x;
  int16_t vanish_y;
  uint8_t player_fly_mode;
  int16_t shadow_height;
  uint16_t hud_rotation;
  uint8_t hud_colour;
  uint8_t hud_damage_flags;
  uint16_t depth_colours;
  uint16_t depth_thresholds;
  uint8_t view_stabilized;
  unsigned active_count;
  unsigned draw_count;
  unsigned unsupported_invisible;
  unsigned unsupported_shadow;
  unsigned unsupported_particle;
  unsigned unsupported_scaled;
  unsigned unsupported_text;
  unsigned unsupported_culled;
  unsigned unsupported_invalid;
  NativeSourceObject objects[kObjPoolCount];
  uint8_t draw_order[kObjPoolCount];
} NativeSourceFrameSnapshot;

typedef struct NativeRendererStats {
  unsigned entries;
  unsigned candidates;
  unsigned drawn;
  unsigned declined_native_ppu;
  unsigned unsupported_invisible;
  unsigned unsupported_shadow;
  unsigned unsupported_particle;
  unsigned unsupported_scaled;
  unsigned unsupported_text;
  unsigned unsupported_culled;
  unsigned unsupported_invalid;
  unsigned decode_failures;
  unsigned vertices;
  unsigned faces;
  unsigned filled_faces;
  unsigned filled_pixels;
  unsigned lines;
  unsigned line_pixels;
  unsigned cockpit_pixels;
  unsigned comms_pixels;
  unsigned meter_pixels;
  unsigned native_world_ready;
  unsigned native_world_suppressed;
} NativeRendererStats;

static NativeSourceFrameSnapshot g_source_snapshot;
static NativeSourceFrameSnapshot g_previous_source_snapshot;
static NativeRendererStats g_last_renderer_stats;
static int g_last_render_width;
static int g_last_render_height;
static uint16_t g_last_render_widescreen_extra;
static uint64_t g_last_semantic_hash;
static int g_source_logic_change_frame;
static int g_source_logic_period_frames = 4;
static uint16_t g_last_pose_alpha_q8;
static uint8_t g_source_interpolation_valid;

static int starfox_enhanced_debug_command(const char *cmd, const char *args,
                                          DebugServerGameSendLine send_line);
static void starfox_enhanced_register_debug_commands(void);

enum {
  kNativeWorldMinActiveObjects = 8,
  kNativeWorldHoldMinActiveObjects = 6,
  kNativeWorldMinDrawnShapes = 2,
  kNativeWorldMinVisiblePixels = 4096,
  kNativeWorldHoldMinVisiblePixels = 2048,
  kPlayerHandle = 1,
  kPlayerFollowCameraNearZ = 96,
  kPlayerFollowCameraMinStableZ = 128,
  kPlayerFollowCameraMaxStableZ = 768,
  kPlayerFollowMaxWorldStep = 512,
};

static bool g_native_world_replacement_active;
static int g_gameplay_hud_hold_until_frame;

void StarFoxDrawPpuFrame(void);

static uint16_t ram_word(uint32_t address) {
  return (uint16_t)g_ram[address] | ((uint16_t)g_ram[address + 1] << 8);
}

static uint8_t ram_byte(uint32_t address) { return g_ram[address]; }

static int8_t ram_i8(uint32_t address) { return (int8_t)ram_byte(address); }

static int16_t ram_i16(uint32_t address) { return (int16_t)ram_word(address); }

static uint64_t fnv1a64_bytes(uint64_t hash, const void *data, size_t size) {
  const uint8_t *bytes = (const uint8_t *)data;
  for (size_t i = 0; i < size; i++) {
    hash ^= bytes[i];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static uint64_t source_snapshot_hash(const NativeSourceFrameSnapshot *snapshot) {
  uint64_t hash = UINT64_C(1469598103934665603);
  if (!snapshot)
    return hash;
  hash = fnv1a64_bytes(hash, snapshot, sizeof(*snapshot));
  hash = fnv1a64_bytes(hash, &g_last_render_width, sizeof(g_last_render_width));
  hash =
      fnv1a64_bytes(hash, &g_last_render_height, sizeof(g_last_render_height));
  hash = fnv1a64_bytes(hash, &g_last_render_widescreen_extra,
                       sizeof(g_last_render_widescreen_extra));
  return hash;
}

static void debug_sendf(DebugServerGameSendLine send_line, const char *fmt, ...) {
  char buffer[2048];
  va_list ap;
  if (!send_line)
    return;
  va_start(ap, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, ap);
  va_end(ap);
  send_line(buffer);
}

static uint16_t gsu_word_from(const SuperFx *fx, uint16_t address) {
  if (!fx || !fx->ram || address + 1 >= fx->ram_size)
    return 0;
  return (uint16_t)fx->ram[address] | ((uint16_t)fx->ram[address + 1] << 8);
}

static SuperFx *current_superfx(void) {
  return g_snes && g_snes->cart ? g_snes->cart->superfx : NULL;
}

static uint16_t gsu_word(uint16_t address) {
  return gsu_word_from(current_superfx(), address);
}

static uint8_t gsu_byte(uint16_t address) {
  const SuperFx *fx = current_superfx();
  if (!fx || !fx->ram || address >= fx->ram_size)
    return 0;
  return fx->ram[address];
}

static int16_t wrap16_i64(int64_t value) {
  return (int16_t)(uint16_t)(uint64_t)value;
}

static int16_t add16(int16_t left, int16_t right) {
  return wrap16_i64((int32_t)left + (int32_t)right);
}

static int16_t subtract16(int16_t left, int16_t right) {
  return wrap16_i64((int32_t)left - (int32_t)right);
}

static int32_t arithmetic_shift_right32(int32_t value, unsigned bits) {
  if (!bits)
    return value;
  if (bits >= 31)
    return value < 0 ? -1 : 0;
  if (value >= 0)
    return value >> bits;
  {
    const int64_t magnitude = -(int64_t)value;
    const int64_t divisor = (int64_t)1 << bits;
    return (int32_t)-((magnitude + divisor - 1) / divisor);
  }
}

static int16_t multiply_q15(int16_t left, int16_t right) {
  return wrap16_i64(
      arithmetic_shift_right32((int32_t)left * (int32_t)right, 15));
}

static int16_t transform_q15_component(const int16_t matrix[9], int16_t x,
                                       int16_t y, int16_t z, unsigned column) {
  int16_t result = multiply_q15(x, matrix[column]);
  result = add16(result, multiply_q15(y, matrix[3 + column]));
  return add16(result, multiply_q15(z, matrix[6 + column]));
}

static int abs_i16_delta(int16_t left, int16_t right) {
  const int delta = (int)subtract16(left, right);
  return delta < 0 ? -delta : delta;
}

static int32_t rounded_q8_delta(int32_t delta, uint16_t alpha_q8) {
  const int64_t scaled = (int64_t)delta * (int64_t)alpha_q8;
  if (scaled >= 0)
    return (int32_t)((scaled + 128) / 256);
  return (int32_t)-(((-scaled) + 128) / 256);
}

static double source_word_difference_f64(double value, double origin) {
  double difference = value - origin;
  while (difference > 32767.0)
    difference -= 65536.0;
  while (difference < -32768.0)
    difference += 65536.0;
  return difference;
}

static double interpolate_i16_f64(int16_t from, int16_t to,
                                  uint16_t alpha_q8) {
  return (double)from + source_word_difference_f64((double)to, (double)from) *
                            ((double)alpha_q8 / 256.0);
}

static uint16_t interpolate_angle_q8(uint16_t from, uint16_t to,
                                     uint16_t alpha_q8) {
  int32_t delta = (int32_t)to - (int32_t)from;
  if (delta > 32767)
    delta -= 65536;
  else if (delta < -32768)
    delta += 65536;
  return (uint16_t)wrap16_i64((int32_t)from +
                              rounded_q8_delta(delta, alpha_q8));
}

static double transform_q15_component_f64(const int16_t matrix[9], double x,
                                          double y, double z,
                                          unsigned column) {
  return (x * (double)matrix[column] + y * (double)matrix[3 + column] +
          z * (double)matrix[6 + column]) /
         32768.0;
}

static void world_to_camera_f64(const int16_t matrix[9], double world_x,
                                double world_y, double world_z, double view_x,
                                double view_y, double view_z, double *out_x,
                                double *out_y, double *out_z) {
  const double relative_x = source_word_difference_f64(world_x, view_x);
  const double relative_y = source_word_difference_f64(world_y, view_y);
  const double relative_z = source_word_difference_f64(world_z, view_z);
  if (out_x)
    *out_x =
        transform_q15_component_f64(matrix, relative_x, relative_y, relative_z,
                                    0);
  if (out_y)
    *out_y =
        transform_q15_component_f64(matrix, relative_x, relative_y, relative_z,
                                    1);
  if (out_z)
    *out_z =
        transform_q15_component_f64(matrix, relative_x, relative_y, relative_z,
                                    2);
}

static NativeSourceObject *
source_snapshot_object_by_handle(NativeSourceFrameSnapshot *snapshot,
                                 uint8_t handle) {
  if (!snapshot)
    return NULL;
  for (unsigned i = 0; i < snapshot->active_count; i++) {
    if (snapshot->objects[i].handle == handle)
      return &snapshot->objects[i];
  }
  return NULL;
}

static const NativeSourceObject *
source_snapshot_const_object_by_handle(const NativeSourceFrameSnapshot *snapshot,
                                       uint8_t handle) {
  if (!snapshot)
    return NULL;
  for (unsigned i = 0; i < snapshot->active_count; i++) {
    if (snapshot->objects[i].handle == handle)
      return &snapshot->objects[i];
  }
  return NULL;
}

static int source_snapshot_view_matrix_matches(
    const NativeSourceFrameSnapshot *left,
    const NativeSourceFrameSnapshot *right) {
  if (!left || !right)
    return 0;
  for (unsigned i = 0; i < 9; i++) {
    if (left->view_matrix[i] != right->view_matrix[i])
      return 0;
  }
  return 1;
}

static int16_t source_object_camera_z_for_view(
    const NativeSourceFrameSnapshot *snapshot,
    const NativeSourceObject *object) {
  const int16_t relative_x = subtract16(object->world_x, snapshot->view_x);
  const int16_t relative_y = subtract16(object->world_y, snapshot->view_y);
  const int16_t relative_z = subtract16(object->world_z, snapshot->view_z);
  return transform_q15_component(snapshot->view_matrix, relative_x, relative_y,
                                 relative_z, 2);
}

static int source_view_is_discontinuous(const NativeSourceFrameSnapshot *left,
                                        const NativeSourceFrameSnapshot *right) {
  const int maximum_continuous_step = 4096;
  if (!left || !right)
    return 1;
  return abs_i16_delta(left->view_x, right->view_x) > maximum_continuous_step ||
         abs_i16_delta(left->view_y, right->view_y) > maximum_continuous_step ||
         abs_i16_delta(left->view_z, right->view_z) > maximum_continuous_step;
}

static void update_source_interpolation_state(
    const NativeSourceFrameSnapshot *snapshot,
    const NativeSourceFrameSnapshot *previous_display_snapshot) {
  const int minimum_period = 1;
  const int maximum_period = 12;

  if (!snapshot || !snapshot->valid) {
    memset(&g_previous_source_snapshot, 0, sizeof(g_previous_source_snapshot));
    g_source_interpolation_valid = 0;
    g_source_logic_change_frame = 0;
    g_source_logic_period_frames = 4;
    return;
  }

  if (!previous_display_snapshot || !previous_display_snapshot->valid) {
    g_previous_source_snapshot = *snapshot;
    g_source_interpolation_valid = 0;
    g_source_logic_change_frame = snapshot->frame;
    return;
  }

  if (snapshot->game_frame == previous_display_snapshot->game_frame)
    return;

  if (source_view_is_discontinuous(snapshot, previous_display_snapshot)) {
    g_previous_source_snapshot = *snapshot;
    g_source_interpolation_valid = 0;
    g_source_logic_change_frame = snapshot->frame;
    return;
  }

  {
    const int period = snapshot->frame - g_source_logic_change_frame;
    if (period >= minimum_period && period <= maximum_period)
      g_source_logic_period_frames = period;
  }
  g_previous_source_snapshot = *previous_display_snapshot;
  g_source_interpolation_valid = g_previous_source_snapshot.valid ? 1u : 0u;
  g_source_logic_change_frame = snapshot->frame;
}

static uint16_t source_interpolation_alpha_q8(void) {
  extern int snes_frame_counter;
  int elapsed = snes_frame_counter - g_source_logic_change_frame;
  if (!g_source_interpolation_valid)
    return 256;
  if (elapsed <= 0)
    return 0;
  if (elapsed >= g_source_logic_period_frames)
    return 256;
  return (uint16_t)((elapsed * 256 + g_source_logic_period_frames / 2) /
                    g_source_logic_period_frames);
}

static bool object_pointer_index(uint16_t pointer, unsigned *index) {
  if (pointer < kObjBase)
    return false;
  const unsigned relative = (unsigned)pointer - kObjBase;
  if (relative >= kObjPoolCount * kObjSize || (relative % kObjSize) != 0)
    return false;
  if (index)
    *index = relative / kObjSize;
  return true;
}

static bool rom_read8_lorom(const uint8_t *rom, size_t rom_size,
                            uint32_t snes_address, uint8_t *out) {
  const uint16_t address = (uint16_t)snes_address;
  if (!rom || !rom_size || address < 0x8000)
    return false;
  {
    const size_t offset = (size_t)((snes_address >> 16) & 0x7fu) * 0x8000u +
                          (size_t)(address & 0x7fffu);
    if (offset >= rom_size)
      return false;
    if (out)
      *out = rom[offset];
    return true;
  }
}

static bool rom_read_i16_lorom(const uint8_t *rom, size_t rom_size,
                               uint32_t snes_address, int16_t *out) {
  uint8_t low = 0;
  uint8_t high = 0;
  if (!rom_read8_lorom(rom, rom_size, snes_address, &low) ||
      !rom_read8_lorom(rom, rom_size, snes_address + 1, &high)) {
    return false;
  }
  if (out)
    *out = (int16_t)((uint16_t)low | ((uint16_t)high << 8));
  return true;
}

static bool read_shape_metadata(const Cart *cart, uint16_t shape,
                                int16_t *sort_z, int16_t *z_max) {
  if (!cart || !cart->rom || !cart->romSize || !shape || shape == kShapeNull)
    return false;
  return rom_read_i16_lorom(cart->rom, cart->romSize, (uint32_t)shape + 5u,
                            sort_z) &&
         rom_read_i16_lorom(cart->rom, cart->romSize, (uint32_t)shape + 14u,
                            z_max);
}

static void put_bgra(uint8_t *pixels, size_t pitch, int width, int height,
                     int x, int y, uint8_t b, uint8_t g, uint8_t r) {
  if (!pixels || x < 0 || y < 0 || x >= width || y >= height)
    return;
  uint8_t *p = pixels + (size_t)y * pitch + (size_t)x * 4u;
  p[0] = b;
  p[1] = g;
  p[2] = r;
  p[3] = 0xff;
}

static void draw_line_h(uint8_t *pixels, size_t pitch, int width, int height,
                        int x0, int x1, int y, uint8_t b, uint8_t g,
                        uint8_t r) {
  if (y < 0 || y >= height)
    return;
  if (x0 > x1) {
    int t = x0;
    x0 = x1;
    x1 = t;
  }
  if (x0 < 0)
    x0 = 0;
  if (x1 >= width)
    x1 = width - 1;
  for (int x = x0; x <= x1; x++)
    put_bgra(pixels, pitch, width, height, x, y, b, g, r);
}

static void draw_line_v(uint8_t *pixels, size_t pitch, int width, int height,
                        int x, int y0, int y1, uint8_t b, uint8_t g,
                        uint8_t r) {
  if (x < 0 || x >= width)
    return;
  if (y0 > y1) {
    int t = y0;
    y0 = y1;
    y1 = t;
  }
  if (y0 < 0)
    y0 = 0;
  if (y1 >= height)
    y1 = height - 1;
  for (int y = y0; y <= y1; y++)
    put_bgra(pixels, pitch, width, height, x, y, b, g, r);
}

static int clamp_int(int v, int lo, int hi) {
  return v < lo ? lo : v > hi ? hi : v;
}

static void draw_debug_probe(uint8_t *pixels, size_t pitch, int width,
                             int height, uint16_t ws_extra) {
  const int view_x = (int16_t)ram_word(kRamViewPosX);
  const int view_y = (int16_t)ram_word(kRamViewPosY);
  const int view_z = (int16_t)ram_word(kRamViewPosZ);
  const int vanish_x = (int16_t)gsu_word(kGsuVanishX);
  const int vanish_y = (int16_t)gsu_word(kGsuVanishY);
  const int native_left = (int)ws_extra;
  const int native_right = native_left + 255;
  int cx = native_left + 16 + vanish_x;
  int cy = vanish_y;

  if (cx < native_left || cx > native_right)
    cx = native_left + 128 + ((view_x >> 5) % 17);
  if (cy < 0 || cy >= height)
    cy = height / 2 + ((view_y >> 5) % 17);
  cx = clamp_int(cx, 0, width - 1);
  cy = clamp_int(cy, 0, height - 1);

  draw_line_h(pixels, pitch, width, height, cx - 5, cx + 5, cy, 0x10, 0xff,
              0xff);
  draw_line_v(pixels, pitch, width, height, cx, cy - 5, cy + 5, 0x10, 0xff,
              0xff);
  draw_line_h(pixels, pitch, width, height, native_left, native_right,
              clamp_int(cy + ((view_z >> 7) % 9) - 4, 0, height - 1), 0x00,
              0x70, 0xa0);

  if (ws_extra) {
    const int left_x = clamp_int((int)ws_extra / 2, 0, width - 1);
    const int right_x = clamp_int(width - 1 - (int)ws_extra / 2, 0, width - 1);
    const int bars[3] = {
        clamp_int(8 + ((view_x & 0xff) >> 2), 8, height - 8),
        clamp_int(8 + ((view_y & 0xff) >> 2), 8, height - 8),
        clamp_int(8 + ((view_z & 0xff) >> 2), 8, height - 8),
    };
    for (int i = 0; i < 3; i++) {
      const int x = left_x + i * 3;
      draw_line_v(pixels, pitch, width, height, x, height - 8 - bars[i] / 2,
                  height - 8, 0xff, 0xd0, 0x20);
      draw_line_v(pixels, pitch, width, height, right_x - i * 3,
                  height - 8 - bars[i] / 2, height - 8, 0xff, 0xd0, 0x20);
    }
  }
}

static bool debug_probe_enabled(void) {
  static int checked;
  static int enabled;
  if (!checked) {
    const char *env = getenv("SNESRECOMP_ENHANCED_RENDERER_DEBUG");
    enabled = env && *env && strcmp(env, "0") != 0;
    checked = 1;
  }
  return enabled != 0;
}

static bool renderer_stats_enabled(void) {
  static int checked;
  static int enabled;
  if (!checked) {
    const char *env = getenv("SNESRECOMP_ENHANCED_RENDERER_STATS");
    enabled = env && *env && strcmp(env, "0") != 0;
    checked = 1;
  }
  return enabled != 0;
}

static bool native_shape_overlay_enabled(void) {
  static int checked;
  static int enabled;
  if (!checked) {
    const char *env = getenv("SNESRECOMP_ENHANCED_NATIVE_SHAPES");
    enabled = !(env && *env && strcmp(env, "0") == 0);
    checked = 1;
  }
  return enabled != 0;
}

static bool native_shape_diagnostics_enabled(void) {
  static int checked;
  static int enabled;
  if (!checked) {
    const char *env = getenv("SNESRECOMP_ENHANCED_NATIVE_SHAPE_DIAGNOSTICS");
    enabled = env && *env && strcmp(env, "0") != 0;
    checked = 1;
  }
  return enabled != 0;
}

static int native_shape_diagnostics_frame(void) {
  static int checked;
  static int target = -1;
  if (!checked) {
    const char *env =
        getenv("SNESRECOMP_ENHANCED_NATIVE_SHAPE_DIAGNOSTICS_FRAME");
    if (env && *env)
      target = atoi(env);
    checked = 1;
  }
  return target;
}

static bool native_world_gate_diagnostics_enabled(void) {
  static int checked;
  static int enabled;
  if (!checked) {
    const char *env = getenv("SNESRECOMP_ENHANCED_NATIVE_WORLD_GATE_LOG");
    enabled = env && *env && strcmp(env, "0") != 0;
    checked = 1;
  }
  return enabled != 0;
}

static void log_native_shape_diagnostic(
    unsigned draw_index, const NativeSourceObject *object,
    const StarFoxEnhancedNativeShapePose *pose,
    const StarFoxEnhancedNativeShapeStats *stats, int visible) {
  if (!native_shape_diagnostics_enabled() || !object || !pose || !stats)
    return;
  extern int snes_frame_counter;
  const int target_frame = native_shape_diagnostics_frame();
  if (target_frame >= 0 && snes_frame_counter != target_frame)
    return;
  if (draw_index == 0) {
    fprintf(stderr,
            "[starfox-native-pose] frame=%d world=(%d,%d,%d) "
            "view=(%d,%d,%d) wmat=[%d,%d,%d;%d,%d,%d;%d,%d,%d]\n",
            snes_frame_counter, (int)object->world_x, (int)object->world_y,
            (int)object->world_z, (int)g_source_snapshot.view_x,
            (int)g_source_snapshot.view_y, (int)g_source_snapshot.view_z,
            (int)g_source_snapshot.view_matrix[0],
            (int)g_source_snapshot.view_matrix[1],
            (int)g_source_snapshot.view_matrix[2],
            (int)g_source_snapshot.view_matrix[3],
            (int)g_source_snapshot.view_matrix[4],
            (int)g_source_snapshot.view_matrix[5],
            (int)g_source_snapshot.view_matrix[6],
            (int)g_source_snapshot.view_matrix[7],
            (int)g_source_snapshot.view_matrix[8]);
  }
  fprintf(stderr,
          "[starfox-native-shape] frame=%d draw_index=%u ptr=%04x "
          "shape=%04x lod=%04x camera=(%.3f,%.3f,%.3f) rot=(%u,%u,%u) "
          "vanish=(%d,%d) ws_extra=%u flags=%02x type=%02x count=%u "
          "sflags=%02x/%02x/%02x/%02x colour=%04x depth_ofs=%u "
          "frames=%u/%u scroll=(%d,%d) decoded=%u/%u visible=%u "
          "decode_failures=%u result=%d\n",
          snes_frame_counter, draw_index, object->pointer, object->shape,
          stats->selected_lod, pose->x, pose->y, pose->z,
          (unsigned)pose->pitch, (unsigned)pose->yaw, (unsigned)pose->roll,
          (int)pose->vanish_x, (int)pose->vanish_y,
          (unsigned)pose->widescreen_extra, (unsigned)object->flags,
          (unsigned)object->type, (unsigned)object->count,
          (unsigned)object->sflags[0], (unsigned)object->sflags[1],
          (unsigned)object->sflags[2], (unsigned)object->sflags[3],
          (unsigned)object->colour_pointer,
          (unsigned)object->object_depth_offset, pose->colour_frame,
          pose->animation_frame, (int)pose->texture_scroll_x,
          (int)pose->texture_scroll_y, stats->decoded_vertices,
          stats->decoded_faces, stats->visible_pixels,
          stats->decode_failures, visible);
}

static void log_renderer_stats(const NativeRendererStats *stats,
                               uint16_t ws_extra, int width, int height) {
  if (!stats || !renderer_stats_enabled())
    return;
  extern int snes_frame_counter;
  if ((snes_frame_counter % 30) != 0)
    return;
  fprintf(stderr,
          "[starfox-native] frame=%d size=%dx%d ws_extra=%u "
          "source=%u/%u snap_frame=%d "
          "entries=%u candidates=%u drawn=%u ready=%u suppress=%u "
          "declined_ppu=%u "
          "unsupported inv=%u shadow=%u particle=%u scaled=%u text=%u "
          "culled=%u invalid=%u decode=%u vertices=%u faces=%u "
          "filled_faces=%u filled_pixels=%u lines=%u line_pixels=%u "
          "cockpit_pixels=%u comms_pixels=%u meter_pixels=%u\n",
          snes_frame_counter, width, height, ws_extra,
          g_source_snapshot.draw_count, g_source_snapshot.active_count,
          g_source_snapshot.frame, stats->entries, stats->candidates,
          stats->drawn, stats->native_world_ready,
          stats->native_world_suppressed, stats->declined_native_ppu,
          stats->unsupported_invisible, stats->unsupported_shadow,
          stats->unsupported_particle, stats->unsupported_scaled,
          stats->unsupported_text, stats->unsupported_culled,
          stats->unsupported_invalid, stats->decode_failures, stats->vertices,
          stats->faces, stats->filled_faces, stats->filled_pixels, stats->lines,
          stats->line_pixels, stats->cockpit_pixels, stats->comms_pixels,
          stats->meter_pixels);
}

static bool source_object_has_drawable_shape(const NativeSourceObject *object) {
  return object && object->shape != 0 && object->shape != kShapeNull &&
         object->metadata_valid && !object->culled &&
         (object->sflags[0] & (kAsfPartObj | kAsfTextObj)) == 0 &&
         (object->sflags[3] & kAsfInvisible4) == 0;
}

static bool source_object_has_native_text(const NativeSourceObject *object) {
  return object && (object->sflags[0] & kAsfTextObj) != 0 &&
         (object->sflags[3] & kAsfInvisible4) == 0;
}

static bool source_object_has_native_shape(const NativeSourceObject *object) {
  return source_object_has_drawable_shape(object) &&
         (object->sflags[0] & kAsfShadowShape) == 0;
}

static bool source_object_has_shadow_shape(const NativeSourceObject *object) {
  return source_object_has_drawable_shape(object) &&
         (object->sflags[0] & kAsfShadowMask) != 0;
}

static uint16_t effective_colour_pointer(const NativeSourceObject *object) {
  const uint8_t flags = object->sflags[0];
  if ((flags & 0x02u) != 0 && (flags & kAsfScaledSprite) == 0) {
    return (flags & 0x01u) != 0 ? kRomColourRed : kRomColourWhite;
  }
  return (flags & 0x01u) != 0 ? kRomColourSpecial : object->colour_pointer;
}

static void source_snapshot_insert_drawable(NativeSourceFrameSnapshot *snapshot,
                                            uint8_t object_index) {
  unsigned position = snapshot->draw_count;
  const NativeSourceObject *candidate = &snapshot->objects[object_index];
  for (unsigned i = 0; i < snapshot->draw_count; i++) {
    const NativeSourceObject *existing =
        &snapshot->objects[snapshot->draw_order[i]];
    const uint16_t difference =
        (uint16_t)(existing->sort_depth - candidate->sort_depth);
    if ((difference & 0x8000u) != 0) {
      position = i;
      break;
    }
  }
  if (position < snapshot->draw_count) {
    memmove(&snapshot->draw_order[position + 1],
            &snapshot->draw_order[position], snapshot->draw_count - position);
  }
  snapshot->draw_order[position] = object_index;
  snapshot->draw_count++;
}

static void latch_source_object(NativeSourceFrameSnapshot *snapshot,
                                uint16_t pointer, unsigned slot) {
  NativeSourceObject object;
  (void)slot;
  memset(&object, 0, sizeof(object));
  object.handle = (uint8_t)(slot + 1u);
  object.pointer = pointer;
  object.shape = ram_word(pointer + kObjShape);
  object.flags = ram_byte(pointer + kObjFlags);
  object.type = ram_byte(pointer + kObjType);
  object.count = ram_byte(pointer + kObjCounter);
  object.world_x = ram_i16(pointer + kObjWorldX);
  object.world_y = ram_i16(pointer + kObjWorldY);
  object.world_z = ram_i16(pointer + kObjWorldZ);
  object.pitch = ram_byte(pointer + kObjRotX);
  object.yaw = ram_byte(pointer + kObjRotY);
  object.roll = ram_byte(pointer + kObjRotZ);
  object.sflags[0] = ram_byte(pointer + kObjSFlags);
  object.sflags[1] = ram_byte(pointer + kObjSFlags2);
  object.sflags[2] = ram_byte(pointer + kObjSFlags3);
  object.sflags[3] = ram_byte(pointer + kObjSFlags4);
  object.object_depth_offset = ram_byte(pointer + kObjAuxDepthOffset);
  object.colour_frame = ram_byte(pointer + kObjAuxColourFrame);
  object.animation_frame = ram_byte(pointer + kObjAuxAnimationFrame);
  object.colour_pointer = ram_word(pointer + kObjAuxColourTable);
  object.texture_scroll_x = ram_i8(pointer + kObjAuxTextureScrollX);
  object.texture_scroll_y = ram_i8(pointer + kObjAuxTextureScrollY);
  object.explosion_count =
      (object.flags & kAfExplosion) != 0 ? object.count : 0;
  snapshot->objects[snapshot->active_count++] = object;
}

static bool classify_and_sort_source_object(NativeSourceFrameSnapshot *snapshot,
                                            const Cart *cart,
                                            unsigned object_index) {
  NativeSourceObject *object = &snapshot->objects[object_index];
  int16_t sort_z = 0;
  int16_t z_max = 0;
  const int16_t relative_x = subtract16(object->world_x, snapshot->view_x);
  const int16_t relative_y = subtract16(object->world_y, snapshot->view_y);
  const int16_t relative_z = subtract16(object->world_z, snapshot->view_z);

  if ((object->sflags[3] & kAsfInvisible4) != 0) {
    snapshot->unsupported_invisible++;
    return false;
  }

  object->camera_x = transform_q15_component(snapshot->view_matrix, relative_x,
                                             relative_y, relative_z, 0);
  object->camera_y = transform_q15_component(snapshot->view_matrix, relative_x,
                                             relative_y, relative_z, 1);
  object->camera_z = transform_q15_component(snapshot->view_matrix, relative_x,
                                             relative_y, relative_z, 2);
  if (object->shape != 0 && object->shape != kShapeNull) {
    object->metadata_valid =
        read_shape_metadata(cart, object->shape, &sort_z, &z_max) ? 1u : 0u;
    if (!object->metadata_valid)
      snapshot->unsupported_invalid++;
  }
  object->sort_depth = add16(object->camera_z, sort_z);
  if ((object->type & kAtGround) != 0)
    object->sort_depth = add16(object->sort_depth, 15000);
  source_snapshot_insert_drawable(snapshot, (uint8_t)object_index);

  if ((object->sflags[0] & kAsfPartObj) != 0) {
    snapshot->unsupported_particle++;
    return false;
  }
  if ((object->sflags[0] & kAsfTextObj) != 0)
    return true;
  if (!object->shape || object->shape == kShapeNull)
    return false;
  if (!object->metadata_valid)
    return false;
  if (add16(object->camera_z, z_max) < 0) {
    object->culled = 1;
    snapshot->unsupported_culled++;
    return false;
  }

  object->colour_pointer = effective_colour_pointer(object);
  return true;
}

static bool latch_source_pool_object(NativeSourceFrameSnapshot *snapshot,
                                     uint16_t pointer, unsigned slot) {
  if (snapshot->active_count >= kObjPoolCount)
    return false;
  latch_source_object(snapshot, pointer, slot);
  return true;
}

static void classify_source_snapshot_objects(NativeSourceFrameSnapshot *snapshot,
                                             const Cart *cart) {
  snapshot->draw_count = 0;
  snapshot->unsupported_invisible = 0;
  snapshot->unsupported_shadow = 0;
  snapshot->unsupported_particle = 0;
  snapshot->unsupported_scaled = 0;
  snapshot->unsupported_text = 0;
  snapshot->unsupported_culled = 0;
  snapshot->unsupported_invalid = 0;
  for (unsigned i = 0; i < snapshot->active_count; i++) {
    snapshot->objects[i].camera_x = 0;
    snapshot->objects[i].camera_y = 0;
    snapshot->objects[i].camera_z = 0;
    snapshot->objects[i].sort_depth = 0;
    snapshot->objects[i].metadata_valid = 0;
    snapshot->objects[i].culled = 0;
    classify_and_sort_source_object(snapshot, cart, i);
  }
}

static bool latch_allst_objects(NativeSourceFrameSnapshot *snapshot,
                                const Cart *cart) {
  bool seen[kObjPoolCount];
  uint16_t pointer = ram_word(kRamAllst);
  (void)cart;
  memset(seen, 0, sizeof(seen));

  while (pointer != 0) {
    unsigned slot = 0;
    uint16_t next = 0;
    if (!object_pointer_index(pointer, &slot) || seen[slot])
      return false;
    seen[slot] = true;
    next = ram_word(pointer + kObjNext);
    if (!latch_source_pool_object(snapshot, pointer, slot))
      return false;
    pointer = next;
  }
  return true;
}

static void stabilize_source_view(NativeSourceFrameSnapshot *snapshot,
                                  const NativeSourceFrameSnapshot *previous) {
  NativeSourceObject *player = source_snapshot_object_by_handle(
      snapshot, (uint8_t)kPlayerHandle);
  const NativeSourceObject *previous_player =
      source_snapshot_const_object_by_handle(previous, (uint8_t)kPlayerHandle);
  int16_t player_camera_z = 0;

  if (!snapshot || !previous || !previous->valid || !player ||
      !previous_player)
    return;
  if (!source_snapshot_view_matrix_matches(snapshot, previous))
    return;
  if (abs_i16_delta(player->world_z, previous_player->world_z) >
      kPlayerFollowMaxWorldStep)
    return;

  player_camera_z = source_object_camera_z_for_view(snapshot, player);
  if (player_camera_z <= 0 || player_camera_z >= kPlayerFollowCameraNearZ)
    return;
  if (previous_player->camera_z < kPlayerFollowCameraMinStableZ ||
      previous_player->camera_z > kPlayerFollowCameraMaxStableZ)
    return;

  snapshot->view_z = subtract16(player->world_z, previous_player->camera_z);
  snapshot->view_stabilized = 1;
}

void StarFoxEnhancedLatchSourceFrame(void) {
  NativeSourceFrameSnapshot snapshot;
  Cart *cart = g_snes ? g_snes->cart : NULL;
  starfox_enhanced_register_debug_commands();
  memset(&snapshot, 0, sizeof(snapshot));

  if (!g_config.enhanced_renderer || !native_shape_overlay_enabled() || !cart ||
      !cart->rom || !cart->romSize)
    goto done;

  extern int snes_frame_counter;
  snapshot.frame = snes_frame_counter;
  snapshot.game_frame = (uint8_t)(ram_byte(kRamGameFrame) & 0x7f);
  snapshot.view_x = ram_i16(kRamViewPosX);
  snapshot.view_y = ram_i16(kRamViewPosY);
  snapshot.view_z = ram_i16(kRamViewPosZ);
  snapshot.raw_view_z = snapshot.view_z;
  for (unsigned i = 0; i < 9; i++)
    snapshot.view_matrix[i] = ram_i16(kRamWmat11W + i * 2u);
  {
    int16_t source_vanish_x = ram_i16(kRamVanishX);
    int16_t source_vanish_y = ram_i16(kRamVanishY);
    const uint16_t gsu_depth_colours = gsu_word(kGsuDepthColours);
    const uint16_t gsu_depth_thresholds = gsu_word(kGsuDepthThresholds);
    /* GSU scratch may be zero outside a source task; WRAM mirrors survive. */
    if (source_vanish_x == 0)
      source_vanish_x = kSourceVanishDefaultX;
    if (source_vanish_y == 0)
      source_vanish_y = kSourceVanishDefaultY;
    snapshot.vanish_x = add16(source_vanish_x, kSuperFxHorizontalInset);
    snapshot.vanish_y = add16(source_vanish_y, kSuperFxVerticalInset);
    snapshot.player_fly_mode = ram_byte(kRamPlayerFlyMode);
    snapshot.shadow_height = ram_i16(kRamShadowHeight);
    if (snapshot.player_fly_mode == 0)
      snapshot.player_fly_mode = (uint8_t)gsu_word(kGsuPlayerFlyMode);
    if (snapshot.shadow_height == 0)
      snapshot.shadow_height = (int16_t)gsu_word(kGsuShadowHeight);
    snapshot.hud_rotation = ram_word(kRamHudRotation);
    snapshot.hud_colour = gsu_byte(kGsuHudColour);
    snapshot.hud_damage_flags = gsu_byte(kGsuHudDamageFlags);
    snapshot.depth_colours =
        gsu_depth_colours != 0 ? gsu_depth_colours : ram_word(kRamDepthTabPtr);
    snapshot.depth_thresholds = gsu_depth_thresholds != 0
                                    ? gsu_depth_thresholds
                                    : kRomDepthThresholdDefault;
  }

  if (!latch_allst_objects(&snapshot, cart)) {
    memset(&snapshot, 0, sizeof(snapshot));
    goto done;
  }
  stabilize_source_view(&snapshot, &g_source_snapshot);
  classify_source_snapshot_objects(&snapshot, cart);
  snapshot.valid = snapshot.active_count != 0;
  update_source_interpolation_state(&snapshot, &g_source_snapshot);

done:
  if (!snapshot.valid)
    update_source_interpolation_state(&snapshot, &g_source_snapshot);
  g_source_snapshot = snapshot;
  g_last_semantic_hash = source_snapshot_hash(&g_source_snapshot);
}

static bool source_snapshot_current(void) {
  if (!g_source_snapshot.valid)
    return false;
  extern int snes_frame_counter;
  const int age = snes_frame_counter - g_source_snapshot.frame;
  return age >= 0 && age <= 1;
}

static bool source_snapshot_has_gameplay_training_world_context(void) {
  if (!source_snapshot_current())
    return false;
  if (g_source_snapshot.active_count < kNativeWorldMinActiveObjects)
    return false;
  return true;
}

static bool source_snapshot_has_live_mode2_context(void) {
  return source_snapshot_current();
}

static bool source_snapshot_is_gameplay_training_world_frame(void) {
  if (!source_snapshot_has_gameplay_training_world_context())
    return false;
  return true;
}

static bool native_world_replacement_ready(const NativeRendererStats *stats) {
  if (!source_snapshot_is_gameplay_training_world_frame() || !stats)
    return false;
  return stats->drawn >= kNativeWorldMinDrawnShapes &&
         stats->filled_pixels >= kNativeWorldMinVisiblePixels;
}

static bool source_snapshot_can_hold_native_world(
    const NativeRendererStats *stats) {
  if (!source_snapshot_current())
    return false;
  if (g_source_snapshot.active_count < kNativeWorldHoldMinActiveObjects)
    return false;
  if (!stats)
    return false;
  return stats->drawn >= kNativeWorldMinDrawnShapes &&
         stats->filled_pixels >= kNativeWorldHoldMinVisiblePixels;
}

static bool update_native_world_replacement(bool raw_ready,
                                            const NativeRendererStats *stats) {
  if (!source_snapshot_can_hold_native_world(stats))
    g_native_world_replacement_active = false;
  else if (raw_ready)
    g_native_world_replacement_active = true;
  return g_native_world_replacement_active;
}

static void log_native_world_gate_transition(const NativeRendererStats *stats,
                                             int raw_ready, int suppress,
                                             int native_ppu_done) {
  static int initialized;
  static int last_raw_ready;
  static int last_suppress;
  static int last_native_ppu_done;
  if (!native_world_gate_diagnostics_enabled() || !stats)
    return;
  if (initialized && last_raw_ready == raw_ready &&
      last_suppress == suppress && last_native_ppu_done == native_ppu_done) {
    return;
  }
  initialized = 1;
  last_raw_ready = raw_ready;
  last_suppress = suppress;
  last_native_ppu_done = native_ppu_done;
  extern int snes_frame_counter;
  fprintf(stderr,
          "[starfox-native-world-gate] frame=%d snap_frame=%d raw_ready=%d "
          "suppress=%d native_ppu_done=%d source=%u/%u drawn=%u pixels=%u "
          "unsupported inv=%u shadow=%u particle=%u scaled=%u text=%u "
          "culled=%u invalid=%u decode=%u\n",
          snes_frame_counter, g_source_snapshot.frame, raw_ready, suppress,
          native_ppu_done, g_source_snapshot.draw_count,
          g_source_snapshot.active_count, stats->drawn, stats->filled_pixels,
          stats->unsupported_invisible, stats->unsupported_shadow,
          stats->unsupported_particle, stats->unsupported_scaled,
          stats->unsupported_text, stats->unsupported_culled,
          stats->unsupported_invalid, stats->decode_failures);
}

static unsigned display_frame(uint8_t object_frame) {
  return (object_frame & 0x80u) != 0 ? (unsigned)(object_frame & 0x7fu)
                                     : (unsigned)g_source_snapshot.game_frame;
}

static void fill_native_shape_pose(StarFoxEnhancedNativeShapePose *pose,
                                   const NativeSourceObject *object,
                                   uint16_t ws_extra, int shadow) {
  const int true_colour_shadow =
      (object->sflags[0] & kAsfShadowShape) != 0;
  uint16_t alpha_q8 = source_interpolation_alpha_q8();
  const NativeSourceObject *previous_object = NULL;
  double view_x = g_source_snapshot.view_x;
  double view_y = g_source_snapshot.view_y;
  double view_z = g_source_snapshot.view_z;
  int16_t view_matrix[9];
  double world_x = object->world_x;
  double world_y = object->world_y;
  double world_z = object->world_z;

  memcpy(view_matrix, g_source_snapshot.view_matrix, sizeof(view_matrix));
  if (g_source_interpolation_valid && alpha_q8 < 256) {
    previous_object = source_snapshot_const_object_by_handle(
        &g_previous_source_snapshot, object->handle);
  }
  if (!previous_object)
    alpha_q8 = 256;
  if (alpha_q8 < 256) {
    view_x = interpolate_i16_f64(g_previous_source_snapshot.view_x,
                                 g_source_snapshot.view_x, alpha_q8);
    view_y = interpolate_i16_f64(g_previous_source_snapshot.view_y,
                                 g_source_snapshot.view_y, alpha_q8);
    view_z = interpolate_i16_f64(g_previous_source_snapshot.view_z,
                                 g_source_snapshot.view_z, alpha_q8);
    world_x =
        interpolate_i16_f64(previous_object->world_x, object->world_x,
                            alpha_q8);
    world_y =
        interpolate_i16_f64(previous_object->world_y, object->world_y,
                            alpha_q8);
    world_z =
        interpolate_i16_f64(previous_object->world_z, object->world_z,
                            alpha_q8);
    StarFoxEnhancedInterpolateMatrixQ15(g_previous_source_snapshot.view_matrix,
                                        g_source_snapshot.view_matrix,
                                        alpha_q8, view_matrix);
  }
  g_last_pose_alpha_q8 = alpha_q8;

  memset(pose, 0, sizeof(*pose));
  if (shadow && !true_colour_shadow) {
    world_to_camera_f64(view_matrix, world_x, g_source_snapshot.shadow_height,
                        world_z, view_x, view_y, view_z, &pose->x, &pose->y,
                        &pose->z);
  } else {
    world_to_camera_f64(view_matrix, world_x, world_y, world_z, view_x, view_y,
                        view_z, &pose->x, &pose->y, &pose->z);
  }
  pose->pitch =
      alpha_q8 < 256
          ? interpolate_angle_q8((uint16_t)previous_object->pitch << 8,
                                 (uint16_t)object->pitch << 8, alpha_q8)
          : (uint16_t)object->pitch << 8;
  pose->yaw =
      alpha_q8 < 256
          ? interpolate_angle_q8((uint16_t)previous_object->yaw << 8,
                                 (uint16_t)object->yaw << 8, alpha_q8)
          : (uint16_t)object->yaw << 8;
  pose->roll =
      alpha_q8 < 256
          ? interpolate_angle_q8((uint16_t)previous_object->roll << 8,
                                 (uint16_t)object->roll << 8, alpha_q8)
          : (uint16_t)object->roll << 8;
  pose->source_pitch = (uint16_t)object->pitch << 8;
  pose->source_yaw = (uint16_t)object->yaw << 8;
  pose->source_roll = (uint16_t)object->roll << 8;
  if (previous_object) {
    pose->previous_source_pitch = (uint16_t)previous_object->pitch << 8;
    pose->previous_source_yaw = (uint16_t)previous_object->yaw << 8;
    pose->previous_source_roll = (uint16_t)previous_object->roll << 8;
    if (alpha_q8 < 256) {
      pose->use_interpolated_object_matrix = 1;
      pose->object_matrix_alpha_q8 = alpha_q8;
    }
  } else {
    pose->previous_source_pitch = pose->source_pitch;
    pose->previous_source_yaw = pose->source_yaw;
    pose->previous_source_roll = pose->source_roll;
  }
  pose->vanish_x = g_source_snapshot.vanish_x;
  pose->vanish_y = g_source_snapshot.vanish_y;
  pose->colour_pointer = object->colour_pointer;
  pose->depth_colours = g_source_snapshot.depth_colours;
  pose->depth_thresholds = g_source_snapshot.depth_thresholds;
  world_to_camera_f64(g_source_snapshot.view_matrix, object->world_x,
                      object->world_y, object->world_z,
                      g_source_snapshot.view_x, g_source_snapshot.view_y,
                      g_source_snapshot.view_z, NULL, NULL,
                      &pose->source_depth_z);
  pose->object_depth_offset = object->object_depth_offset;
  pose->explosion_progress = object->explosion_count;
  pose->use_source_view_matrix = 1;
  pose->use_source_depth_z = 1;
  pose->texture_scroll_x = object->texture_scroll_x;
  pose->texture_scroll_y = object->texture_scroll_y;
  if ((object->sflags[0] & kAsfScaledSprite) != 0) {
    pose->simple_scaled_sprite = 1;
    pose->simple_sprite_colour = object->object_depth_offset;
  }
  pose->animation_frame = display_frame(object->animation_frame);
  pose->colour_frame = display_frame(object->colour_frame);
  memcpy(pose->source_view_matrix, view_matrix, sizeof(pose->source_view_matrix));
  pose->widescreen_extra = ws_extra;
  if (shadow) {
    pose->use_shadow_shape = 1;
    pose->flatten_shadow_matrix = 1;
    if (!true_colour_shadow) {
      pose->force_colour = 1;
      pose->forced_colour = kShadowForcedColour;
    }
  }
}

static unsigned draw_projected_text_object(uint8_t *pixels, size_t pitch,
                                           int width, int height,
                                           const Cart *cart,
                                           uint16_t ws_extra,
                                           const NativeSourceObject *object) {
  StarFoxEnhancedNativeShapePose pose;
  fill_native_shape_pose(&pose, object, ws_extra, 0);
  return StarFoxEnhancedDrawProjectedText(
      pixels, pitch, width, height, cart->rom, cart->romSize,
      object->colour_pointer, object->object_depth_offset,
      object->texture_scroll_x, &pose);
}

static unsigned draw_comms_hud(uint8_t *pixels, size_t pitch, int width,
                               int height) {
  Cart *cart = g_snes ? g_snes->cart : NULL;
  const uint8_t open_count = ram_byte(kRamMsgCount1);
  const uint8_t animation_count = ram_byte(kRamMsgCount2);
  const uint8_t friend_id = ram_byte(kRamWhichFriend);
  const uint16_t face_pointer = gsu_word(kGsuFacePtr);
  const uint32_t text_address =
      (uint32_t)kRomMessagesBank | (uint32_t)ram_word(kRamFriendsMsg);
  if (!cart || !cart->rom || !cart->romSize)
    return 0;
  return StarFoxEnhancedDrawCommsHud(
      pixels, pitch, width, height, cart->rom, cart->romSize, open_count,
      animation_count, friend_id, face_pointer, text_address);
}

static unsigned draw_gameplay_hud_meters(uint8_t *pixels, size_t pitch,
                                         int width, int height,
                                         uint16_t ws_extra) {
  const uint8_t damage = gsu_byte(kGsuDamage);
  const uint8_t boost = gsu_byte(kGsuBoostAnim);
  return StarFoxEnhancedDrawGameplayHudMeters(
      pixels, pitch, width, height, ws_extra, damage ? damage : 36u,
      boost ? boost : 36u,
      gsu_byte(kGsuShieldUp) != 0 || ram_word(kRamShieldUp) != 0,
      source_snapshot_current(), gsu_byte(kGsuBossHp), gsu_byte(kGsuBossMaxHp));
}

static unsigned draw_native_shape_object(uint8_t *pixels, size_t pitch,
                                         int width, int height,
                                         const Cart *cart, uint16_t ws_extra,
                                         unsigned draw_index,
                                         const NativeSourceObject *object,
                                         int shadow,
                                         NativeRendererStats *renderer_stats) {
  StarFoxEnhancedNativeShapePose pose;
  StarFoxEnhancedNativeShapeStats stats;
  if (renderer_stats)
    renderer_stats->candidates++;
  fill_native_shape_pose(&pose, object, ws_extra, shadow);
  const int shape_visible = StarFoxEnhancedDrawNativeShape(
      pixels, pitch, width, height, cart->rom, cart->romSize, object->shape,
      &pose, &stats);
  log_native_shape_diagnostic(draw_index, object, &pose, &stats, shape_visible);
  if (renderer_stats) {
    renderer_stats->filled_pixels += stats.visible_pixels;
    renderer_stats->decode_failures += stats.decode_failures;
    renderer_stats->vertices += stats.decoded_vertices;
    renderer_stats->faces += stats.decoded_faces;
  }
  if (!shape_visible)
    return 0;
  if (renderer_stats)
    renderer_stats->drawn++;
  return 1;
}

static unsigned
draw_source_snapshot_shapes(uint8_t *pixels, size_t pitch, int width,
                            int height, uint16_t ws_extra,
                            NativeRendererStats *renderer_stats) {
  Cart *cart = g_snes ? g_snes->cart : NULL;
  if (!source_snapshot_current() || !cart || !cart->rom || !cart->romSize ||
      !pixels)
    return 0;

  unsigned drawn = 0;
  if (renderer_stats) {
    renderer_stats->unsupported_invisible =
        g_source_snapshot.unsupported_invisible;
    renderer_stats->unsupported_shadow = g_source_snapshot.unsupported_shadow;
    renderer_stats->unsupported_particle =
        g_source_snapshot.unsupported_particle;
    renderer_stats->unsupported_scaled = g_source_snapshot.unsupported_scaled;
    renderer_stats->unsupported_text = g_source_snapshot.unsupported_text;
    renderer_stats->unsupported_culled = g_source_snapshot.unsupported_culled;
    renderer_stats->unsupported_invalid = g_source_snapshot.unsupported_invalid;
  }

  /* Shadow pass */
  if ((g_source_snapshot.player_fly_mode & kPfmShadows) != 0) {
    for (unsigned i = 0; i < g_source_snapshot.draw_count; i++) {
      const NativeSourceObject *object =
          &g_source_snapshot.objects[g_source_snapshot.draw_order[i]];
      if (source_object_has_shadow_shape(object)) {
        drawn += draw_native_shape_object(pixels, pitch, width, height, cart,
                                          ws_extra, i, object, 1,
                                          renderer_stats);
      }
    }
  }

  /* Normal object pass */
  for (unsigned i = 0; i < g_source_snapshot.draw_count; i++) {
    const NativeSourceObject *object =
        &g_source_snapshot.objects[g_source_snapshot.draw_order[i]];
    if (renderer_stats)
      renderer_stats->entries++;
    if (source_object_has_native_shape(object)) {
      drawn += draw_native_shape_object(pixels, pitch, width, height, cart,
                                        ws_extra, i, object, 0,
                                        renderer_stats);
    } else if (source_object_has_native_text(object)) {
      drawn += draw_projected_text_object(pixels, pitch, width, height, cart,
                                          ws_extra, object) != 0 ? 1u : 0u;
    }
  }
  if ((g_source_snapshot.hud_rotation & 0x8000u) != 0) {
    const uint8_t override =
        g_source_snapshot.hud_colour >= 128u ? g_source_snapshot.hud_colour
                                            : 0u;
    const unsigned cockpit_pixels = StarFoxEnhancedDrawCockpitHud(
        pixels, pitch, width, height, cart->rom, cart->romSize,
        (uint8_t)g_source_snapshot.hud_rotation,
        g_source_snapshot.hud_colour, g_source_snapshot.hud_damage_flags,
        (int)ws_extra + kSuperFxHorizontalInset, kSuperFxVerticalInset,
        override);
    drawn += cockpit_pixels != 0 ? 1u : 0u;
    if (renderer_stats)
      renderer_stats->cockpit_pixels += cockpit_pixels;
  }
  return drawn;
}

static void debug_native_shape_stats(const NativeSourceObject *object, int shadow,
                                     StarFoxEnhancedNativeShapeStats *stats) {
  Cart *cart = g_snes ? g_snes->cart : NULL;
  uint8_t pixel[4] = {0, 0, 0, 0};
  StarFoxEnhancedNativeShapePose pose;
  if (stats)
    memset(stats, 0, sizeof(*stats));
  if (!object || !cart || !cart->rom || !cart->romSize)
    return;
  fill_native_shape_pose(&pose, object, g_last_render_widescreen_extra, shadow);
  StarFoxEnhancedDrawNativeShape(pixel, sizeof(pixel), 1, 1, cart->rom,
                                 cart->romSize, object->shape, &pose, stats);
}

static void debug_send_status(DebugServerGameSendLine send_line) {
  extern int snes_frame_counter;
  debug_sendf(send_line,
              "ok frame=%d logic=%u flow=recomp width=%d height=%d objects=%u "
              "draw=%u rendered=%u pixels=%u hash=%016llx",
              source_snapshot_current() ? g_source_snapshot.frame
                                        : snes_frame_counter,
              (unsigned)g_source_snapshot.game_frame, g_last_render_width,
              g_last_render_height, g_source_snapshot.active_count,
              g_source_snapshot.draw_count, g_last_renderer_stats.drawn,
              g_last_renderer_stats.filled_pixels,
              (unsigned long long)g_last_semantic_hash);
}

static void debug_send_objects(DebugServerGameSendLine send_line) {
  NativeSourceFrameSnapshot snapshot = g_source_snapshot;
  for (unsigned i = 0; i < snapshot.active_count; i++) {
    const NativeSourceObject *object = &snapshot.objects[i];
    debug_sendf(send_line,
                "object handle=%u shape=%x flags=%x type=%x "
                "sflags=%x,%x,%x,%x world=%d,%d,%d rot=%u,%u,%u "
                "anim=%u colour_frame=%u colour_table=%x tex=%d,%d",
                (unsigned)object->handle, (unsigned)object->shape,
                (unsigned)object->flags, (unsigned)object->type,
                (unsigned)object->sflags[0], (unsigned)object->sflags[1],
                (unsigned)object->sflags[2], (unsigned)object->sflags[3],
                (int)object->world_x, (int)object->world_y,
                (int)object->world_z, (unsigned)object->pitch,
                (unsigned)object->yaw, (unsigned)object->roll,
                (unsigned)object->animation_frame,
                (unsigned)object->colour_frame,
                (unsigned)object->colour_pointer,
                (int)object->texture_scroll_x, (int)object->texture_scroll_y);
  }
  send_line("objects-end");
}

static void debug_send_draw_order(DebugServerGameSendLine send_line) {
  char buffer[2048];
  size_t used = 0;
  NativeSourceFrameSnapshot snapshot = g_source_snapshot;
  used += (size_t)snprintf(buffer + used, sizeof(buffer) - used, "draw_order");
  for (unsigned i = 0; i < snapshot.draw_count && used < sizeof(buffer); i++) {
    const uint8_t object_index = snapshot.draw_order[i];
    const unsigned handle =
        object_index < snapshot.active_count
            ? (unsigned)snapshot.objects[object_index].handle
            : (unsigned)object_index;
    used += (size_t)snprintf(buffer + used, sizeof(buffer) - used, " %u",
                             handle);
  }
  buffer[sizeof(buffer) - 1] = 0;
  send_line(buffer);
}

static void debug_send_pose(DebugServerGameSendLine send_line,
                            const NativeSourceObject *object, unsigned handle,
                            int shadow) {
  StarFoxEnhancedNativeShapePose pose;
  StarFoxEnhancedNativeShapeStats stats;
  fill_native_shape_pose(&pose, object, g_last_render_widescreen_extra, shadow);
  debug_native_shape_stats(object, shadow, &stats);
  debug_sendf(send_line,
              "pose handle=%u shape=%x lod=%x colour=%x world=%d,%d,%d "
              "camera=%.3f,%.3f,%.3f rot=%u,%u,%u type=%x sflags=%x,%x,%x,%x "
              "shadow=%d particle=%d text=%d scaled=%d alpha_q8=%u "
              "vertices=%u faces=%u",
              handle, (unsigned)object->shape, (unsigned)stats.selected_lod,
              (unsigned)pose.colour_pointer, (int)object->world_x,
              (int)object->world_y, (int)object->world_z, pose.x,
              pose.y, pose.z, (unsigned)pose.pitch,
              (unsigned)pose.yaw, (unsigned)pose.roll, (unsigned)object->type,
              (unsigned)object->sflags[0], (unsigned)object->sflags[1],
              (unsigned)object->sflags[2], (unsigned)object->sflags[3], shadow,
              (!shadow && (object->sflags[0] & kAsfPartObj) != 0) ? 1 : 0,
              (!shadow && (object->sflags[0] & kAsfTextObj) != 0) ? 1 : 0,
              (!shadow && (object->sflags[0] & kAsfScaledSprite) != 0) ? 1 : 0,
              (unsigned)g_last_pose_alpha_q8, stats.decoded_vertices,
              stats.decoded_faces);
}

static void debug_send_poses(DebugServerGameSendLine send_line) {
  NativeSourceFrameSnapshot snapshot = g_source_snapshot;
  if ((snapshot.player_fly_mode & kPfmShadows) != 0) {
    for (unsigned i = 0; i < snapshot.draw_count; i++) {
      const uint8_t object_index = snapshot.draw_order[i];
      if (object_index >= snapshot.active_count)
        continue;
      const NativeSourceObject *object = &snapshot.objects[object_index];
      if (source_object_has_shadow_shape(object))
        debug_send_pose(send_line, object, (unsigned)object->handle, 1);
    }
  }
  for (unsigned i = 0; i < snapshot.draw_count; i++) {
    const uint8_t object_index = snapshot.draw_order[i];
    if (object_index >= snapshot.active_count)
      continue;
    const NativeSourceObject *object = &snapshot.objects[object_index];
    if (source_object_has_native_shape(object))
      debug_send_pose(send_line, object, (unsigned)object->handle, 0);
  }
  send_line("poses-end");
}

static void debug_send_ppu(DebugServerGameSendLine send_line) {
  Ppu *ppu = g_ppu;
  if (!ppu) {
    send_line("ppu unavailable");
    return;
  }
  debug_sendf(send_line,
              "ppu mode=%u main=%x obsel=%x bg1_chr=%x bg1_scr=%x "
              "bg2_chr=%x bg2_scr=%x bg3_chr=%x bg3_scr=%x "
              "bg2_vofs=%u bg2_hofs=%u brightness=%u dots=0",
              (unsigned)(ppu->bgmode & 0x07u),
              (unsigned)ppu->screenEnabled[0], (unsigned)ppu->obsel,
              (unsigned)((ppu->bgTileAdr >> 0) & 0xfu),
              (unsigned)ppu->bgXsc[0], (unsigned)((ppu->bgTileAdr >> 4) & 0xfu),
              (unsigned)ppu->bgXsc[1], (unsigned)((ppu->bgTileAdr >> 8) & 0xfu),
              (unsigned)ppu->bgXsc[2], 0u, 0u, (unsigned)PPU_brightness(ppu));
}

static void debug_send_source_view(DebugServerGameSendLine send_line) {
  NativeSourceFrameSnapshot snapshot = g_source_snapshot;
  debug_sendf(send_line,
              "source frame=%d logic=%u valid=%u view=%d,%d,%d "
              "raw_view_z=%d stabilized=%u "
              "interp_valid=%u interp_alpha_q8=%u interp_period=%d "
              "interp_origin=%d "
              "wmat=%d,%d,%d,%d,%d,%d,%d,%d,%d vanish=%d,%d "
              "shadow_height=%d fly_mode=%02x",
              snapshot.frame, (unsigned)snapshot.game_frame,
              (unsigned)snapshot.valid, (int)snapshot.view_x,
              (int)snapshot.view_y, (int)snapshot.view_z,
              (int)snapshot.raw_view_z, (unsigned)snapshot.view_stabilized,
              (unsigned)g_source_interpolation_valid,
              (unsigned)source_interpolation_alpha_q8(),
              g_source_logic_period_frames, g_source_logic_change_frame,
              (int)snapshot.view_matrix[0], (int)snapshot.view_matrix[1],
              (int)snapshot.view_matrix[2], (int)snapshot.view_matrix[3],
              (int)snapshot.view_matrix[4], (int)snapshot.view_matrix[5],
              (int)snapshot.view_matrix[6], (int)snapshot.view_matrix[7],
              (int)snapshot.view_matrix[8], (int)snapshot.vanish_x,
              (int)snapshot.vanish_y, (int)snapshot.shadow_height,
              (unsigned)snapshot.player_fly_mode);
}

static void debug_send_comms(DebugServerGameSendLine send_line) {
  debug_sendf(send_line,
              "comms msg1=%u msg2=%u friend=%u msg=%04x text=%06x "
              "face=%04x active=%u text_visible=%u pixels=%u",
              (unsigned)ram_byte(kRamMsgCount1),
              (unsigned)ram_byte(kRamMsgCount2),
              (unsigned)ram_byte(kRamWhichFriend),
              (unsigned)ram_word(kRamFriendsMsg),
              (unsigned)(kRomMessagesBank | ram_word(kRamFriendsMsg)),
              (unsigned)gsu_word(kGsuFacePtr),
              (unsigned)(ram_byte(kRamMsgCount1) != 0 ||
                         ram_byte(kRamMsgCount2) != 0),
              (unsigned)(ram_byte(kRamMsgCount1) != 0 &&
                         ram_byte(kRamMsgCount2) >= 5),
              g_last_renderer_stats.comms_pixels);
}

static void debug_send_render_stats(DebugServerGameSendLine send_line) {
  debug_sendf(send_line,
              "render_stats active=%u draw_order=%u candidates=%u "
              "rendered=%u particles=%u invalid=%u culled=%u text=%u "
              "scaled=%u pixels=%u decode=%u vertices=%u faces=%u "
              "cockpit_pixels=%u comms_pixels=%u meter_pixels=%u "
              "declined_ppu=%u native_ready=%u native_suppressed=%u",
              g_source_snapshot.active_count, g_source_snapshot.draw_count,
              g_last_renderer_stats.candidates, g_last_renderer_stats.drawn,
              g_source_snapshot.unsupported_particle,
              g_source_snapshot.unsupported_invalid,
              g_source_snapshot.unsupported_culled,
              g_source_snapshot.unsupported_text,
              g_source_snapshot.unsupported_scaled,
              g_last_renderer_stats.filled_pixels,
              g_last_renderer_stats.decode_failures,
              g_last_renderer_stats.vertices, g_last_renderer_stats.faces,
              g_last_renderer_stats.cockpit_pixels,
              g_last_renderer_stats.comms_pixels,
              g_last_renderer_stats.meter_pixels,
              g_last_renderer_stats.declined_native_ppu,
              g_last_renderer_stats.native_world_ready,
              g_last_renderer_stats.native_world_suppressed);
}

static int starfox_enhanced_debug_command(const char *cmd, const char *args,
                                          DebugServerGameSendLine send_line) {
  (void)args;
  if (!cmd || !send_line)
    return 0;
  if (strcmp(cmd, "status") == 0 || strcmp(cmd, "state") == 0) {
    debug_send_status(send_line);
    return 1;
  }
  if (strcmp(cmd, "objects") == 0) {
    debug_send_objects(send_line);
    return 1;
  }
  if (strcmp(cmd, "draw_order") == 0) {
    debug_send_draw_order(send_line);
    return 1;
  }
  if (strcmp(cmd, "poses") == 0) {
    debug_send_poses(send_line);
    return 1;
  }
  if (strcmp(cmd, "ppu") == 0) {
    debug_send_ppu(send_line);
    return 1;
  }
  if (strcmp(cmd, "source_view") == 0) {
    debug_send_source_view(send_line);
    return 1;
  }
  if (strcmp(cmd, "render_stats") == 0) {
    debug_send_render_stats(send_line);
    return 1;
  }
  if (strcmp(cmd, "comms") == 0) {
    debug_send_comms(send_line);
    return 1;
  }
  if (strcmp(cmd, "frame_hash") == 0) {
    debug_sendf(send_line, "hash=%016llx",
                (unsigned long long)g_last_semantic_hash);
    return 1;
  }
  if (strcmp(cmd, "window") == 0) {
    debug_send_status(send_line);
    send_line("window-end");
    return 1;
  }
  return 0;
}

static void starfox_enhanced_register_debug_commands(void) {
  static int registered;
  if (registered)
    return;
  debug_server_set_game_command_handler(starfox_enhanced_debug_command);
  registered = 1;
}

static void clear_frame(uint8_t *pixels, size_t pitch, int width, int height) {
  if (!pixels || width <= 0 || height <= 0)
    return;
  for (int y = 0; y < height; y++)
    memset(pixels + (size_t)y * pitch, 0, (size_t)width * 4u);
}

static uint8_t *allocate_bgra_scratch(int width, int height, size_t *pitch) {
  size_t scratch_pitch = 0;
  if (pitch)
    *pitch = 0;
  if (width <= 0 || height <= 0)
    return NULL;
  scratch_pitch = (size_t)width * 4u;
  if (pitch)
    *pitch = scratch_pitch;
  return (uint8_t *)calloc((size_t)height, scratch_pitch);
}

static void composite_bgra_nonzero(uint8_t *dst, size_t dst_pitch,
                                   const uint8_t *src, size_t src_pitch,
                                   int width, int height) {
  if (!dst || !src || width <= 0 || height <= 0 ||
      dst_pitch < (size_t)width * 4u || src_pitch < (size_t)width * 4u)
    return;
  for (int y = 0; y < height; y++) {
    uint8_t *dst_row = dst + (size_t)y * dst_pitch;
    const uint8_t *src_row = src + (size_t)y * src_pitch;
    for (int x = 0; x < width; x++) {
      const uint8_t *s = src_row + (size_t)x * 4u;
      if (s[0] == 0 && s[1] == 0 && s[2] == 0 && s[3] == 0)
        continue;
      memcpy(dst_row + (size_t)x * 4u, s, 4u);
    }
  }
}

static void copy_stock_center(const RtlEnhancedRendererFrame *frame) {
  if (!frame || !frame->pixels || !g_ppu || !g_ppu->renderBuffer ||
      !g_ppu->renderPitch || frame->width <= 0 || frame->height <= 0)
    return;
  const int copy_width =
      frame->width - (int)frame->widescreen_extra >= 256 ? 256 : frame->width;
  const int dst_left = frame->width >= 256 + 2 * (int)frame->widescreen_extra
                           ? (int)frame->widescreen_extra
                           : 0;
  if (copy_width <= 0 || dst_left < 0 || dst_left + copy_width > frame->width)
    return;
  const int copy_height = frame->height < 224 ? frame->height : 224;
  for (int y = 0; y < copy_height; y++) {
    memcpy(frame->pixels + (size_t)y * frame->pitch + (size_t)dst_left * 4u,
           g_ppu->renderBuffer + (size_t)y * g_ppu->renderPitch,
           (size_t)copy_width * 4u);
  }
}

static unsigned overlay_stock_superfx_comms_region(
    uint8_t *dst, size_t dst_pitch, int width, int height) {
  if (!dst || !g_ppu || !g_ppu->renderBuffer || !g_ppu->renderPitch ||
      width <= 0 || height <= 0)
    return 0;
  const int src_face_left = kSuperFxHorizontalInset + 48;
  const int src_face_right = kSuperFxHorizontalInset + 80;
  const int src_text_left = kSuperFxHorizontalInset + 82;
  const int src_text_right = kSuperFxHorizontalInset + 176;
  const int src_top = kSuperFxVerticalInset + 152;
  const int src_bottom = kSuperFxVerticalInset + 192;
  const int ui_left = (width - 224) / 2;
  unsigned text_pixels = 0;
  for (int y = src_top; y < src_bottom && y < height; y++) {
    const uint8_t *src_row = g_ppu->renderBuffer + (size_t)y * g_ppu->renderPitch;
    for (int src_x = src_text_left; src_x < src_text_right; src_x++) {
      const uint8_t *s = src_row + (size_t)src_x * 4u;
      const unsigned luminance = (unsigned)s[0] + (unsigned)s[1] +
                                 (unsigned)s[2];
      if (luminance >= 360u)
        text_pixels++;
    }
  }
  if (text_pixels < 16u)
    return 0;
  unsigned visible = 0;
  for (int y = src_top; y < src_bottom && y < height; y++) {
    const uint8_t *src_row = g_ppu->renderBuffer + (size_t)y * g_ppu->renderPitch;
    uint8_t *dst_row = dst + (size_t)y * dst_pitch;
    for (int src_x = src_face_left; src_x < src_text_right; src_x++) {
      const int in_face = src_x >= src_face_left && src_x < src_face_right;
      const int in_text = src_x >= src_text_left && src_x < src_text_right;
      if (!in_face && !in_text)
        continue;
      const int dst_x =
          ui_left + (in_face ? 48 + (src_x - src_face_left)
                             : 82 + (src_x - src_text_left));
      if (dst_x < 0 || dst_x >= width)
        continue;
      const uint8_t *s = src_row + (size_t)src_x * 4u;
      if (s[0] < 16 && s[1] < 16 && s[2] < 16)
        continue;
      if (in_face && s[1] > s[2] + 24 && s[1] > s[0] + 24)
        continue;
      if (in_text) {
        const unsigned luminance = (unsigned)s[0] + (unsigned)s[1] +
                                   (unsigned)s[2];
        if (luminance < 360u)
          continue;
      }
      memcpy(dst_row + (size_t)dst_x * 4u, s, 4u);
      visible++;
    }
  }
  return visible;
}

static int stock_meter_pixel_is_hud(const uint8_t *s) {
  if (!s || (s[0] < 16 && s[1] < 16 && s[2] < 16))
    return 0;
  if (s[1] > s[2] + 24 && s[1] > s[0] + 24)
    return 0;
  return ((unsigned)s[0] + (unsigned)s[1] + (unsigned)s[2]) >= 120u;
}

static unsigned overlay_stock_superfx_meter(uint8_t *dst, size_t dst_pitch,
                                            int width, int height,
                                            int source_x, int source_y,
                                            int target_x, int target_y,
                                            int meter_width,
                                            int meter_height) {
  if (!dst || !g_ppu || !g_ppu->renderBuffer || !g_ppu->renderPitch ||
      width <= 0 || height <= 0 || meter_width <= 0 || meter_height <= 0)
    return 0;
  unsigned visible = 0;
  for (int y = 0; y < meter_height; y++) {
    const int src_y = source_y + y;
    const int dst_y = target_y + y;
    if (src_y < 0 || src_y >= height || dst_y < 0 || dst_y >= height)
      continue;
    const uint8_t *src_row =
        g_ppu->renderBuffer + (size_t)src_y * g_ppu->renderPitch;
    uint8_t *dst_row = dst + (size_t)dst_y * dst_pitch;
    for (int x = 0; x < meter_width; x++) {
      const int src_x = source_x + x;
      const int dst_x = target_x + x;
      if (src_x < 0 || src_x >= width || dst_x < 0 || dst_x >= width)
        continue;
      const uint8_t *s = src_row + (size_t)src_x * 4u;
      if (!stock_meter_pixel_is_hud(s))
        continue;
      memcpy(dst_row + (size_t)dst_x * 4u, s, 4u);
      visible++;
    }
  }
  return visible;
}

static unsigned overlay_stock_superfx_hud_meters(uint8_t *dst, size_t dst_pitch,
                                                 int width, int height) {
  const int src_y = kSuperFxVerticalInset + 176;
  const int shield_src_x = kSuperFxHorizontalInset + 8;
  const int boost_src_x = kSuperFxHorizontalInset + 176;
  unsigned visible = 0;
  visible += overlay_stock_superfx_meter(dst, dst_pitch, width, height,
                                         shield_src_x, src_y, 24, 192, 40, 8);
  visible += overlay_stock_superfx_meter(dst, dst_pitch, width, height,
                                         boost_src_x, src_y, width - 64, 192,
                                         40, 8);
  return visible;
}

static bool native_frame_looks_suspect(const RtlEnhancedRendererFrame *frame) {
  if (!frame || !frame->pixels || frame->width <= 0 || frame->height <= 0 ||
      !frame->widescreen_extra)
    return false;

  bool seen[64];
  memset(seen, 0, sizeof(seen));
  unsigned unique = 0;
  unsigned frame_non_black = 0;
  unsigned margin_non_black = 0;
  const unsigned total = (unsigned)frame->width * (unsigned)frame->height;
  const int margin_left = (int)frame->widescreen_extra;
  const int margin_right = frame->width - (int)frame->widescreen_extra;
  const unsigned margin_total =
      (unsigned)(margin_left * 2) * (unsigned)frame->height;
  for (int y = 0; y < frame->height; y += 2) {
    const uint8_t *row = frame->pixels + (size_t)y * frame->pitch;
    for (int x = 0; x < frame->width; x += 2) {
      const uint8_t *p = row + (size_t)x * 4u;
      const uint8_t b = p[0];
      const uint8_t g = p[1];
      const uint8_t r = p[2];
      if (r < 16 && g < 16 && b < 16)
        continue;
      frame_non_black += 4;
      if (x < margin_left || x >= margin_right)
        margin_non_black += 4;
      const uint8_t key = (uint8_t)(((r & 0xc0u) >> 2) | ((g & 0xc0u) >> 4) |
                                    ((b & 0xc0u) >> 6));
      if (!seen[key]) {
        seen[key] = true;
        unique++;
      }
    }
  }
  if (margin_total != 0 && margin_non_black > margin_total / 128u &&
      unique > 4u)
    return true;
  return frame_non_black > total / 128u && unique > 8u;
}

static void dump_bgra_bmp(const char *path, const uint8_t *pixels, size_t pitch,
                          int width, int height) {
  if (!path || !*path || !pixels || pitch < (size_t)width * 4u || width <= 0 ||
      height <= 0)
    return;
  FILE *f = fopen(path, "wb");
  if (!f)
    return;
  const uint32_t image_size = (uint32_t)width * (uint32_t)height * 4u;
  const uint32_t header_size = 14u + 40u;
  const uint32_t file_size = header_size + image_size;
  uint8_t hdr[54] = {'B', 'M'};
  const int32_t bmp_width = width;
  const int32_t bmp_height = -height;
  const uint16_t planes = 1;
  const uint16_t bpp = 32;
  const uint32_t dib_size = 40;
  memcpy(hdr + 2, &file_size, 4);
  memcpy(hdr + 10, &header_size, 4);
  memcpy(hdr + 14, &dib_size, 4);
  memcpy(hdr + 18, &bmp_width, 4);
  memcpy(hdr + 22, &bmp_height, 4);
  memcpy(hdr + 26, &planes, 2);
  memcpy(hdr + 28, &bpp, 2);
  memcpy(hdr + 34, &image_size, 4);
  fwrite(hdr, 1, sizeof(hdr), f);
  for (int y = 0; y < height; y++)
    fwrite(pixels + (size_t)y * pitch, 1, (size_t)width * 4u, f);
  fclose(f);
}

static void dump_bgra_png(const char *path, const uint8_t *pixels, size_t pitch,
                          int width, int height) {
  if (!path || !*path || !pixels || pitch < (size_t)width * 4u || width <= 0 ||
      height <= 0)
    return;

  uint8_t *rgba = (uint8_t *)malloc((size_t)width * (size_t)height * 4u);
  if (!rgba)
    return;
  for (int y = 0; y < height; y++) {
    const uint8_t *src = pixels + (size_t)y * pitch;
    uint8_t *dst = rgba + (size_t)y * (size_t)width * 4u;
    for (int x = 0; x < width; x++) {
      dst[x * 4 + 0] = src[x * 4 + 2];
      dst[x * 4 + 1] = src[x * 4 + 1];
      dst[x * 4 + 2] = src[x * 4 + 0];
      dst[x * 4 + 3] = 0xff;
    }
  }
  stbi_write_png(path, width, height, 4, rgba, width * 4);
  free(rgba);
}

static void dump_bgra_auto(const char *path, const RtlEnhancedRendererFrame *frame) {
  if (!path || !frame)
    return;
  const size_t path_len = strlen(path);
  if (path_len >= 4 && strcmp(path + path_len - 4, ".png") == 0)
    dump_bgra_png(path, frame->pixels, frame->pitch, frame->width,
                  frame->height);
  else
    dump_bgra_bmp(path, frame->pixels, frame->pitch, frame->width,
                  frame->height);
}

static void maybe_dump_frame(const RtlEnhancedRendererFrame *frame) {
  static int checked;
  static const char *path;
  static int target = -1;
  static int dumped;
  static const char *dir;
  static int dir_next = -1;
  static int dir_end = -1;
  static int dir_step = 60;
  if (!checked) {
    checked = 1;
    path = getenv("SNESRECOMP_ENHANCED_FRAME_BMP");
    const char *target_env = getenv("SNESRECOMP_ENHANCED_FRAME_BMP_FRAME");
    if (target_env && *target_env)
      target = atoi(target_env);
    dir = getenv("SNESRECOMP_ENHANCED_FRAME_BMP_DIR");
    if (dir && *dir) {
      const char *start_env = getenv("SNESRECOMP_ENHANCED_FRAME_BMP_START");
      const char *end_env = getenv("SNESRECOMP_ENHANCED_FRAME_BMP_END");
      const char *step_env = getenv("SNESRECOMP_ENHANCED_FRAME_BMP_STEP");
      dir_next = start_env && *start_env ? atoi(start_env) : 0;
      dir_end = end_env && *end_env ? atoi(end_env) : 0x7fffffff;
      dir_step = step_env && *step_env ? atoi(step_env) : 60;
      if (dir_step <= 0)
        dir_step = 60;
    }
  }
  if ((!path || dumped) && (!dir || !*dir || dir_next < 0))
    return;
  extern int snes_frame_counter;
  if (path && !dumped) {
    if (target < 0 || snes_frame_counter >= target) {
      dump_bgra_auto(path, frame);
      dumped = 1;
    }
  }
  if (dir && *dir && dir_next >= 0) {
    while (snes_frame_counter >= dir_next && dir_next <= dir_end) {
      char dump_path[512];
      snprintf(dump_path, sizeof(dump_path), "%s/frame_%06d.bmp", dir,
               dir_next);
      dump_bgra_auto(dump_path, frame);
      dir_next += dir_step;
    }
  }
}

RtlEnhancedRenderResult
StarFoxEnhancedRenderFrame(RtlEnhancedRendererFrame *frame) {
  if (!frame)
    return kRtlEnhancedRender_NotHandled;
  if (frame->default_renderer_done)
    return kRtlEnhancedRender_Handled;
  const bool shape_overlay_enabled = native_shape_overlay_enabled();
  NativeRendererStats stats;
  size_t native_world_pitch = 0;
  uint8_t *native_world = NULL;
  unsigned drawn = 0;
  bool native_world_ready = false;
  bool suppress_superfx_world_bg1 = false;
  memset(&stats, 0, sizeof(stats));

  if (shape_overlay_enabled) {
    native_world =
        allocate_bgra_scratch(frame->width, frame->height, &native_world_pitch);
    if (native_world) {
      drawn = draw_source_snapshot_shapes(native_world, native_world_pitch,
                                          frame->width, frame->height,
                                          frame->widescreen_extra, &stats);
      native_world_ready = native_world_replacement_ready(&stats);
      suppress_superfx_world_bg1 =
          update_native_world_replacement(native_world_ready, &stats);
      stats.native_world_ready = native_world_ready ? 1u : 0u;
      stats.native_world_suppressed = suppress_superfx_world_bg1 ? 1u : 0u;
    }
  } else {
    g_native_world_replacement_active = false;
    g_gameplay_hud_hold_until_frame = 0;
  }

  clear_frame(frame->pixels, frame->pitch, frame->width, frame->height);
  StarFoxDrawPpuFrame();
  extern int snes_frame_counter;
  if (frame->widescreen_extra != 0 &&
      (native_world_ready || suppress_superfx_world_bg1)) {
    g_gameplay_hud_hold_until_frame = snes_frame_counter + 90;
  } else if (frame->widescreen_extra == 0) {
    g_gameplay_hud_hold_until_frame = 0;
  }
  const bool gameplay_hud_frame = frame->widescreen_extra != 0 &&
                                  snes_frame_counter <=
                                      g_gameplay_hud_hold_until_frame;
  int native_ppu_done = StarFoxEnhancedDrawNativePpuLayers(
      frame->pixels, frame->pitch, frame->width, frame->height,
      frame->widescreen_extra, suppress_superfx_world_bg1 ? 1 : 0,
      gameplay_hud_frame ? 1 : 0);
  const bool mode2_scene_frame =
      frame->widescreen_extra != 0 && g_ppu && PPU_mode(g_ppu) == 2 &&
      source_snapshot_has_live_mode2_context();
  const bool mode2_transition_frame =
      frame->widescreen_extra != 0 && g_ppu && PPU_mode(g_ppu) == 2 &&
      !source_snapshot_has_live_mode2_context();
  if (!suppress_superfx_world_bg1 && native_ppu_done &&
      (mode2_transition_frame ||
       (!mode2_scene_frame && native_frame_looks_suspect(frame)))) {
    clear_frame(frame->pixels, frame->pitch, frame->width, frame->height);
    native_ppu_done = 0;
    stats.declined_native_ppu++;
  }
  log_native_world_gate_transition(&stats, native_world_ready ? 1 : 0,
                                   suppress_superfx_world_bg1 ? 1 : 0,
                                   native_ppu_done);
  if (!native_ppu_done && !suppress_superfx_world_bg1 && !mode2_scene_frame)
    copy_stock_center(frame);
  if (shape_overlay_enabled) {
    const bool mode2_native_overlay =
        mode2_scene_frame && drawn != 0;
    if (suppress_superfx_world_bg1 || mode2_native_overlay) {
      composite_bgra_nonzero(frame->pixels, frame->pitch, native_world,
                             native_world_pitch, frame->width, frame->height);
    }
    if (gameplay_hud_frame) {
      const unsigned meter_pixels = draw_gameplay_hud_meters(
          frame->pixels, frame->pitch, frame->width, frame->height,
          frame->widescreen_extra);
      stats.meter_pixels = meter_pixels;
      if (meter_pixels < 32u)
        stats.meter_pixels += overlay_stock_superfx_hud_meters(
            frame->pixels, frame->pitch, frame->width, frame->height);
      stats.comms_pixels =
          draw_comms_hud(frame->pixels, frame->pitch, frame->width,
                         frame->height);
      if (stats.comms_pixels == 0)
        stats.comms_pixels = overlay_stock_superfx_comms_region(
            frame->pixels, frame->pitch, frame->width, frame->height);
      StarFoxEnhancedDrawGameplayHudSprites(frame->pixels, frame->pitch,
                                            frame->width, frame->height,
                                            frame->widescreen_extra);
    }
    log_renderer_stats(&stats, frame->widescreen_extra, frame->width,
                       frame->height);
  }
  if (!drawn && debug_probe_enabled()) {
    draw_debug_probe(frame->pixels, frame->pitch, frame->width, frame->height,
                     frame->widescreen_extra);
  }
  g_last_renderer_stats = stats;
  g_last_render_width = frame->width;
  g_last_render_height = frame->height;
  g_last_render_widescreen_extra = frame->widescreen_extra;
  g_last_semantic_hash = source_snapshot_hash(&g_source_snapshot);
  free(native_world);
  maybe_dump_frame(frame);
  return kRtlEnhancedRender_Handled;
}
