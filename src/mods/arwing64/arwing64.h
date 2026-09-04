/* Arwing64: draw the Star Fox 64 Arwing in place of the Super FX player ship.
 *
 * Runtime side of the mod. Owns the owner-asset cache (extracted once from
 * the user's Star Fox 64 ROM, hash-verified on every load), the guarded ROM
 * patch that makes the guest draw an invisible player shape, the guest-state
 * readers (wing damage, boost, brake, barrel roll, view mode, damage flash),
 * and the host_mesh draw of the ship at the player's slot in the Star Fox
 * Enhanced draw order. Everything fails closed: if the cache or ROM is
 * unavailable the stock Super FX ship is left untouched.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "starfox_enhanced_native.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum Arwing64Status {
  kArwing64_Disabled = 0,   /* feature off in config */
  kArwing64_NoRom,          /* no ROM path configured */
  kArwing64_RomInvalid,     /* ROM failed identity/size checks */
  kArwing64_ExtractFailed,  /* decoder error */
  kArwing64_CacheInvalid,   /* cache present but hash mismatch and re-extract failed */
  kArwing64_PatchFailed,    /* guest ROM bytes unexpected; ship left stock */
  kArwing64_Active,
} Arwing64Status;

typedef struct Arwing64GuestState {
  uint8_t wing_state;      /* $70:2B26: 0 both, 1 left gone, 2 right gone, 3 both gone */
  uint8_t pshipflags;      /* $14D6 */
  uint8_t pshipflags2;     /* $14D7 */
  uint8_t pshipflags3;     /* $14D8 */
  uint8_t view_mode;       /* $14DB */
  uint8_t gameflags;       /* $14D0 */
  uint8_t gameflags2;      /* $14D1 */
  uint8_t gamemode;        /* $18C2 bit0 = space */
  int8_t roll_velocity;    /* $1501 */
  int8_t roll_offset;      /* $1502 */
  uint8_t flash_count;     /* $1527 */
  uint8_t flash_type;      /* $1528 */
  uint16_t boost_anim;     /* $70:01BC, 40 = full */
  uint16_t boost_charge;   /* $70:01BA */
  uint16_t damage;         /* $70:01B8 */
  uint16_t player_object;  /* $1238 */
  uint8_t player_pitch;    /* object +$12 */
  uint8_t player_yaw;      /* +$13 */
  uint8_t player_roll;     /* +$14 */
  int8_t pitch_delta;      /* per logic frame */
  int8_t roll_delta;
  int boosting, braking, rolling, cockpit, in_game, dying;
} Arwing64GuestState;

typedef struct Arwing64Stats {
  Arwing64Status status;
  const char *status_detail;
  uint32_t frames_drawn;
  uint32_t frames_skipped_cockpit;
  uint32_t last_pixels;
  uint32_t last_triangles;
  int last_bbox[4];
  uint32_t glow_draws;
  uint32_t shield_draws;
  uint32_t patch_mismatches;
  int patch_applied;
  char cache_path[512];
  char blob_sha256[65];
} Arwing64Stats;

/* Called at startup and whenever the feature toggles; idempotent. Performs
 * cache load / extraction and applies or reverts the guest ROM patch. */
void arwing64_refresh(void);

/* True when the mesh is loaded and the guest patch is applied. */
int arwing64_active(void);

/* True when the mod wants the enhanced frame path to run even though the
 * user's presentation is Authentic (so the ship can be composited over the
 * stock frame). */
int arwing64_wants_enhanced_frame(void);

/* Draw the player's Arwing into a BGRA target at the given Enhanced pose
 * (camera-space position, source angles, view matrix, vanishing point). The
 * target may be the transparent native-world scratch (transparent_black=1) or
 * an opaque frame (0). Returns the number of pixels written. */
uint32_t arwing64_draw_player(uint8_t *pixels, size_t pitch, int width,
                              int height, const uint8_t *rom, size_t rom_size,
                              const StarFoxEnhancedNativeShapePose *pose,
                              int transparent_black);

/* Per presentation frame bookkeeping (call once after the guest frame). */
void arwing64_post_frame(void);

void arwing64_read_guest_state(Arwing64GuestState *out);
const Arwing64Stats *arwing64_stats(void);
const char *arwing64_status_name(Arwing64Status status);

/* Debug server sub-command: "arwing [status|state|cache|patch]". Returns 1
 * when handled. */
typedef void (*Arwing64SendLine)(const char *line);
int arwing64_debug_command(const char *args, Arwing64SendLine send_line);

#ifdef __cplusplus
}
#endif
