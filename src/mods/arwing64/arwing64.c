#include "arwing64.h"

#include "arwing64_audio.h"
#include "arwing64_extract.h"
#include "sf64_rom.h"
#include "sf64_audio_render.h"

#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "config.h"
#include "arwing64_picture.h"
#include "host_mesh.h"
#include "host_paths.h"
#include "sha256.h"
#include "snes/cart.h"
#include "snes/snes.h"
#include "snes/superfx.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define arwing64_mkdir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define arwing64_mkdir(p) mkdir(p, 0755)
#endif

/* ---- retail WRAM / GSU addresses (docs/ARWING64_SOURCE_SEAMS.md) -------- */
enum {
  kRamGameFlags = 0x14d0,
  kRamGameFlags2 = 0x14d1,
  kRamPshipFlags = 0x14d6,
  kRamPshipFlags2 = 0x14d7,
  kRamPshipFlags3 = 0x14d8,
  kRamViewMode = 0x14db,
  kRamRollVelocity = 0x1501,
  kRamRollOffset = 0x1502,
  kRamFlashCount = 0x1527,
  kRamFlashType = 0x1528,
  kRamPlayerObject = 0x1238,
  kRamGameMode = 0x18c2,
  kGsuDamage = 0x01b8,
  kGsuBoostCharge = 0x01ba,
  kGsuBoostAnim = 0x01bc,
  kGsuWingState = 0x2b26,
  kObjRotX = 0x12,
  kObjRotY = 0x13,
  kObjRotZ = 0x14,
  kObjPoolBase = 0x0336,
  kObjPoolEnd = 0x0336 + 0x36 * 70,
  /* Read-only retail identity guard, ArwingModelIDTable row 0. */
  kRomShapeTable = 0x300d5,
  kRomShapeTablePatchBytes = 6,
};

static const uint8_t kShapeTableExpected[kRomShapeTablePatchBytes] = {
    0x20, 0xd3, 0xac, 0xd3, 0x74, 0xd3};
/* N64 model units -> SNES units: N64 wingspan 96 (x -48..48) to the Super FX
 * MYSHIP_4 wingspan 72 (x -36..36). */
static const float kModelScale = 72.0f / 96.0f;
/* Star Fox projection: screen = vanish + trunc(coord * 256 / z). */
static const float kFocalLength = 256.0f;
static const float kNearZ = 16.0f;

typedef struct Runtime {
  int refreshed_for_enabled;
  char configured_rom[1024];
  int configured_sfx;
  HostMesh *mesh;
  Arwing64Stats stats;
  int list_broken_a, list_broken_b, list_wing_a, list_wing_b;
  int list_orb[4], list_shield;
  int pose_half_open, pose_closed, pose_open;
  int list_laser[2];
  Arwing64GuestState guest;
  uint8_t prev_pitch, prev_yaw, prev_roll;
  int prev_valid;
  uint32_t frame_counter;
  float flap_pitch_deg, flap_roll_deg; /* smoothed */
  /* Debug overrides (arwing force ...): -1 = follow the guest. */
  int force_wing, force_roll, force_boost, force_brake, force_flash;
} Runtime;

static Runtime g_rt;

/* ---- helpers ------------------------------------------------------------ */

static uint8_t ram8(uint32_t a) { return g_ram[a & 0x1ffffu]; }
static uint16_t ram16(uint32_t a) {
  return (uint16_t)(ram8(a) | (ram8(a + 1) << 8));
}
static uint16_t gsu16(uint16_t a) {
  if (!g_snes || !g_snes->cart || !g_snes->cart->superfx ||
      !g_snes->cart->superfx->ram)
    return 0;
  const uint8_t *ram = g_snes->cart->superfx->ram;
  return (uint16_t)(ram[a] | (ram[(uint16_t)(a + 1)] << 8));
}

static void set_status(Arwing64Status status, const char *detail) {
  g_rt.stats.status = status;
  g_rt.stats.status_detail = detail;
}

const char *arwing64_status_name(Arwing64Status status) {
  switch (status) {
  case kArwing64_Disabled: return "disabled";
  case kArwing64_NoRom: return "no-rom";
  case kArwing64_RomInvalid: return "rom-invalid";
  case kArwing64_ExtractFailed: return "extract-failed";
  case kArwing64_CacheInvalid: return "cache-invalid";
  case kArwing64_RetailMismatch: return "retail-mismatch";
  case kArwing64_Active: return "active";
  }
  return "unknown";
}

static void hex64(const uint8_t digest[32], char out[65]) {
  static const char hex[] = "0123456789abcdef";
  for (int i = 0; i < 32; i++) {
    out[i * 2] = hex[digest[i] >> 4];
    out[i * 2 + 1] = hex[digest[i] & 15];
  }
  out[64] = 0;
}

static uint8_t *read_whole_file(const char *path, size_t *size) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }
  const long len = ftell(f);
  rewind(f);
  if (len <= 0 || len > (64L << 20)) {
    fclose(f);
    return NULL;
  }
  uint8_t *buf = (uint8_t *)malloc((size_t)len);
  if (buf && fread(buf, 1, (size_t)len, f) != (size_t)len) {
    free(buf);
    buf = NULL;
  }
  fclose(f);
  if (buf) *size = (size_t)len;
  return buf;
}

static int write_whole_file(const char *path, const void *data, size_t size) {
  FILE *f = fopen(path, "wb");
  if (!f) return 0;
  int ok = fwrite(data, 1, size, f) == size;
  if (fclose(f)) ok = 0;
  return ok;
}

/* ---- cache -------------------------------------------------------------- */

/* Cache layout: <exe>/arwing64_cache/arwing64_<rom sha1[0:12]>.bin and a
 * .sha256 sidecar with the blob hash. The sidecar lets a partially written
 * or tampered blob fail closed and trigger a fresh extraction. */
static int cache_paths(const char *rom_sha1, char *blob_path, size_t n,
                       char *sidecar_path, size_t m) {
  char dir[512];
  if (!snesrecomp_exe_dir_path("arwing64_cache", dir, sizeof(dir))) return 0;
  arwing64_mkdir(dir);
  snprintf(blob_path, n, "%s/arwing64_%.12s_v2.bin", dir, rom_sha1);
  snprintf(sidecar_path, m, "%s/arwing64_%.12s_v2.bin.sha256", dir, rom_sha1);
  return 1;
}

static HostMesh *load_blob_verified(const uint8_t *blob, size_t size,
                                    const char *expected_hex) {
  uint8_t digest[32];
  char hex[65];
  sha256_compute(blob, size, digest);
  hex64(digest, hex);
  if (expected_hex && strncmp(hex, expected_hex, 64) != 0) return NULL;
  const char *err = NULL;
  HostMesh *mesh = host_mesh_load(blob, size, &err);
  if (!mesh) {
    set_status(kArwing64_CacheInvalid, err);
    return NULL;
  }
  if (host_mesh_find_display_list(mesh, "aLaserShotGreenDL") < 0 ||
      host_mesh_find_display_list(mesh, "aLaserShotBlueDL") < 0) {
    host_mesh_free(mesh);
    set_status(kArwing64_CacheInvalid, "mesh cache is missing laser assets");
    return NULL;
  }
  snprintf(g_rt.stats.blob_sha256, sizeof(g_rt.stats.blob_sha256), "%s", hex);
  return mesh;
}

static void resolve_lists(void) {
  const HostMesh *m = g_rt.mesh;
  g_rt.list_wing_a = host_mesh_find_display_list(m, "aAwRightWingDL");
  g_rt.list_wing_b = host_mesh_find_display_list(m, "aAwLeftWingDL");
  g_rt.list_broken_a = host_mesh_find_display_list(m, "aAwRightWingBrokenDL");
  g_rt.list_broken_b = host_mesh_find_display_list(m, "aAwLeftWingBrokenDL");
  g_rt.list_orb[0] = host_mesh_find_display_list(m, "aOrbDL_red");
  g_rt.list_orb[1] = host_mesh_find_display_list(m, "aOrbDL_blue");
  g_rt.list_orb[2] = host_mesh_find_display_list(m, "aOrbDL_green");
  g_rt.list_orb[3] = host_mesh_find_display_list(m, "aOrbDL_orange");
  g_rt.list_shield = host_mesh_find_display_list(m, "aBarrelRollDL");
  g_rt.list_laser[0] = host_mesh_find_display_list(m, "aLaserShotGreenDL");
  g_rt.list_laser[1] = host_mesh_find_display_list(m, "aLaserShotBlueDL");
  g_rt.pose_half_open = host_mesh_find_pose(m, "wings_half_open");
  g_rt.pose_closed = host_mesh_find_pose(m, "wings_closed");
  g_rt.pose_open = host_mesh_find_pose(m, "wings_open");
}

static int build_mesh_from_rom(const char *rom_path) {
  size_t rom_size = 0;
  uint8_t *rom_bytes = read_whole_file(rom_path, &rom_size);
  if (!rom_bytes) {
    set_status(kArwing64_RomInvalid, "cannot read the configured Star Fox 64 ROM");
    return 0;
  }
  Sf64Rom rom;
  const char *err = NULL;
  if (!sf64_rom_load_memory(&rom, rom_bytes, rom_size, &err)) {
    free(rom_bytes);
    set_status(kArwing64_RomInvalid, err);
    return 0;
  }
  char blob_path[600], sidecar_path[600];
  const int have_cache_dir =
      cache_paths(rom.sha1_hex, blob_path, sizeof(blob_path), sidecar_path,
                  sizeof(sidecar_path));
  if (!have_cache_dir) {
    sf64_rom_free(&rom); free(rom_bytes);
    set_status(kArwing64_CacheInvalid, "cannot locate mesh cache"); return 0;
  }
  if (g_config.arwing64_sfx) {
    char dir[512], manifest[600];
    if (!have_cache_dir || !snesrecomp_exe_dir_path("arwing64_cache", dir, sizeof(dir))) {
      sf64_rom_free(&rom); free(rom_bytes);
      set_status(kArwing64_CacheInvalid, "cannot create audio cache"); return 0;
    }
    snprintf(manifest, sizeof(manifest), "%s/audio/manifest.sha256", dir);
    FILE *existing = fopen(manifest, "rb");
    int had_manifest = existing != NULL;
    if (existing) fclose(existing);
    if (!(had_manifest ? sf64_audio_cache_valid(dir) : sf64_audio_extract(&rom, dir, &err))) {
      sf64_rom_free(&rom); free(rom_bytes);
      set_status(kArwing64_CacheInvalid, had_manifest ? "audio cache corrupt; remove arwing64_cache to rebuild" : err);
      return 0;
    }
  }
  sf64_rom_free(&rom);
  if (have_cache_dir) {
    snprintf(g_rt.stats.cache_path, sizeof(g_rt.stats.cache_path), "%s",
             blob_path);
    size_t blob_size = 0, side_size = 0;
    uint8_t *blob = read_whole_file(blob_path, &blob_size);
    uint8_t *side = read_whole_file(sidecar_path, &side_size);
    int had_cache = blob != NULL || side != NULL;
    if (blob && side && side_size >= 64) {
      char expected[65];
      memcpy(expected, side, 64);
      expected[64] = 0;
      g_rt.mesh = load_blob_verified(blob, blob_size, expected);
    }
    free(blob);
    free(side);
    if (g_rt.mesh) {
      free(rom_bytes);
      resolve_lists();
      return 1;
    }
    if (had_cache) {
      free(rom_bytes);
      set_status(kArwing64_CacheInvalid, "mesh cache corrupt; remove arwing64_cache to rebuild");
      return 0;
    }
  }
  /* Extract afresh from the ROM image. */
  uint8_t *blob = NULL;
  size_t blob_size = 0;
  Arwing64ExtractStats st;
  if (!arwing64_extract_mesh(rom_bytes, rom_size, &blob, &blob_size, &st,
                             &err)) {
    free(rom_bytes);
    set_status(kArwing64_ExtractFailed, err);
    return 0;
  }
  free(rom_bytes);
  g_rt.mesh = load_blob_verified(blob, blob_size, NULL);
  if (!g_rt.mesh) {
    free(blob);
    set_status(kArwing64_ExtractFailed, "extracted blob failed validation");
    return 0;
  }
  if (have_cache_dir) {
    /* Write blob then sidecar; an incomplete committed cache fails closed. */
    if (!write_whole_file(blob_path, blob, blob_size) ||
        !write_whole_file(sidecar_path, g_rt.stats.blob_sha256, 64)) {
      free(blob); host_mesh_free(g_rt.mesh); g_rt.mesh = NULL;
      set_status(kArwing64_CacheInvalid, "cannot write mesh cache"); return 0;
    }
  }
  free(blob);
  resolve_lists();
  return 1;
}

/* Read-only retail seam guard: the game ROM is never patched. */
static int verify_guest_rom(void) {
  if (!g_snes || !g_snes->cart || !g_snes->cart->rom ||
      g_snes->cart->romSize < kRomShapeTable + kRomShapeTablePatchBytes ||
      memcmp(g_snes->cart->rom + kRomShapeTable, kShapeTableExpected,
             kRomShapeTablePatchBytes)) {
    g_rt.stats.retail_mismatches++;
    set_status(kArwing64_RetailMismatch, "retail player table does not match");
    return 0;
  }
  return 1;
}

/* ---- lifecycle ------------------------------------------------------------ */

static void reset_overrides(void) {
  g_rt.force_wing = -1;
  g_rt.force_roll = 0;
  g_rt.force_boost = 0;
  g_rt.force_brake = 0;
  g_rt.force_flash = 0;
}

void arwing64_refresh(void) {
  static int overrides_initialised;
  if (!overrides_initialised) {
    reset_overrides();
    overrides_initialised = 1;
  }
  const int enabled = g_config.arwing64_enabled ? 1 : 0;
  if (strcmp(g_rt.configured_rom, g_config.arwing64_rom_path) ||
      g_rt.configured_sfx != (g_config.arwing64_sfx ? 1 : 0)) {
    arwing64_audio_refresh(NULL, 0);
    arwing64_picture_reset();
    host_mesh_free(g_rt.mesh); g_rt.mesh = NULL;
    g_rt.stats.cache_path[0] = 0;
    snprintf(g_rt.configured_rom, sizeof(g_rt.configured_rom), "%s", g_config.arwing64_rom_path);
    g_rt.configured_sfx = g_config.arwing64_sfx ? 1 : 0;
  }
  if (!enabled) {
    arwing64_audio_refresh(NULL, 0);
    arwing64_picture_reset();
    if (g_rt.mesh) {
      host_mesh_free(g_rt.mesh);
      g_rt.mesh = NULL;
    }
    set_status(kArwing64_Disabled, NULL);
    g_rt.refreshed_for_enabled = 0;
    return;
  }
  if (!g_rt.mesh) {
    /* Cache/ROM failures stay failed until an explicit refresh or toggle.
     * Do not rehash a bad 12 MiB ROM on every presentation frame. */
    g_rt.refreshed_for_enabled = 1;
    if (!g_config.arwing64_rom_path[0]) {
      set_status(kArwing64_NoRom, "set Arwing64Rom in config.ini or pick the ROM in the launcher");
      return;
    }
    if (!build_mesh_from_rom(g_config.arwing64_rom_path)) return;
  }
  if (!verify_guest_rom()) { arwing64_audio_refresh(NULL, 0); return; }
  set_status(kArwing64_Active, NULL);
  g_rt.refreshed_for_enabled = 1;
  {
    /* Audio clips live beside the mesh blob in the cache directory. */
    char dir[512];
    dir[0] = 0;
    if (g_rt.stats.cache_path[0]) {
      snprintf(dir, sizeof(dir), "%s", g_rt.stats.cache_path);
      char *slash = strrchr(dir, '/');
      char *bslash = strrchr(dir, '\\');
      char *cut = !slash ? bslash : !bslash ? slash : slash > bslash ? slash : bslash;
      if (cut) *cut = 0;
    }
    if (!arwing64_audio_refresh(dir, g_config.arwing64_sfx ? 1 : 0)) {
      arwing64_picture_reset();
      set_status(kArwing64_CacheInvalid, "cannot activate complete SF64 audio set");
    }
  }
}

int arwing64_active(void) {
  if (strcmp(g_rt.configured_rom, g_config.arwing64_rom_path) ||
      g_rt.configured_sfx != (g_config.arwing64_sfx ? 1 : 0)) arwing64_refresh();
  if (g_config.arwing64_enabled && !g_rt.refreshed_for_enabled) arwing64_refresh();
  if (!g_config.arwing64_enabled && g_rt.mesh) arwing64_refresh();
  return g_rt.stats.status == kArwing64_Active && g_rt.mesh != NULL &&
         g_snes != NULL;
}

int arwing64_wants_enhanced_frame(void) {
  return g_config.arwing64_enabled ? 1 : 0;
}

/* ---- guest state ---------------------------------------------------------- */

void arwing64_read_guest_state(Arwing64GuestState *out) {
  memset(out, 0, sizeof(*out));
  out->wing_state = (uint8_t)(gsu16(kGsuWingState) & 3u);
  out->pshipflags = ram8(kRamPshipFlags);
  out->pshipflags2 = ram8(kRamPshipFlags2);
  out->pshipflags3 = ram8(kRamPshipFlags3);
  out->view_mode = ram8(kRamViewMode);
  out->gameflags = ram8(kRamGameFlags);
  out->gameflags2 = ram8(kRamGameFlags2);
  out->gamemode = ram8(kRamGameMode);
  out->roll_velocity = (int8_t)ram8(kRamRollVelocity);
  out->roll_offset = (int8_t)ram8(kRamRollOffset);
  out->flash_count = ram8(kRamFlashCount);
  out->flash_type = ram8(kRamFlashType);
  out->boost_anim = gsu16(kGsuBoostAnim);
  out->boost_charge = gsu16(kGsuBoostCharge);
  out->damage = gsu16(kGsuDamage);
  out->player_object = ram16(kRamPlayerObject);
  if (out->player_object >= kObjPoolBase && out->player_object < kObjPoolEnd) {
    out->player_pitch = ram8(out->player_object + kObjRotX);
    out->player_yaw = ram8(out->player_object + kObjRotY);
    out->player_roll = ram8(out->player_object + kObjRotZ);
  }
  /* The wing bits in pshipflags are authoritative when the GSU mirror has
   * not been refreshed yet this frame. */
  if (out->wing_state == 0) {
    if (out->pshipflags & 0x08) out->wing_state |= 1;
    if (out->pshipflags & 0x10) out->wing_state |= 2;
  }
  out->boosting = (out->pshipflags2 & 0x20) != 0;
  out->braking = (out->pshipflags2 & 0x40) != 0;
  out->rolling = out->roll_velocity != 0;
  out->cockpit = out->view_mode == 3;
  out->in_game = (out->gameflags2 & 0x08) != 0;
  out->dying = (out->gameflags & 0x42) != 0;
}

void arwing64_post_frame(void) {
  if (!g_config.arwing64_enabled) return;
  Arwing64GuestState g;
  arwing64_read_guest_state(&g);
  arwing64_audio_frame(g.rolling, (g.gameflags2 & 8) != 0 && (g.gameflags & 0xc0) == 0);
  if (g_rt.prev_valid && g.player_object) {
    g.pitch_delta = (int8_t)(g.player_pitch - g_rt.prev_pitch);
    g.roll_delta = (int8_t)(g.player_roll - g_rt.prev_roll);
  }
  g_rt.prev_pitch = g.player_pitch;
  g_rt.prev_yaw = g.player_yaw;
  g_rt.prev_roll = g.player_roll;
  g_rt.prev_valid = g.player_object != 0;
  if (g_rt.force_wing >= 0) g.wing_state = (uint8_t)(g_rt.force_wing & 3);
  if (g_rt.force_roll != 0) {
    g.roll_velocity = (int8_t)g_rt.force_roll;
    g.rolling = 1;
  }
  if (g_rt.force_boost > 0) g.boosting = 1;
  if (g_rt.force_brake > 0) g.braking = 1;
  if (g_rt.force_flash > 0) g.flash_count = (uint8_t)g_rt.force_flash;
  g_rt.guest = g;
  g_rt.frame_counter++;
  /* Flap deflection follows the stick like the N64 game: pitch input moves
   * all four flaps together (elevator), roll input moves the pairs against
   * each other (aileron). Smoothed so the 20 Hz logic does not snap. */
  const float target_pitch = fmaxf(-22.0f, fminf(22.0f, (float)g.pitch_delta * 6.0f));
  const float target_roll = fmaxf(-22.0f, fminf(22.0f, (float)g.roll_velocity * 0.7f));
  g_rt.flap_pitch_deg += (target_pitch - g_rt.flap_pitch_deg) * 0.35f;
  g_rt.flap_roll_deg += (target_roll - g_rt.flap_roll_deg) * 0.35f;
}

/* ---- drawing ---------------------------------------------------------------- */

typedef struct Projection {
  float vanish_x, vanish_y;
} Projection;

static void project_starfox(void *ctx, float x, float y, float z, float *sx,
                            float *sy) {
  const Projection *p = (const Projection *)ctx;
  const float pz = z == 0.0f ? 1.0f : z;
  *sx = p->vanish_x + truncf(x * kFocalLength / pz);
  *sy = p->vanish_y + truncf(y * kFocalLength / pz);
}

typedef struct LimbCtx {
  int wing_state;
  float flap_pitch, flap_roll;
} LimbCtx;

/* Mirror of Display_ArwingOverrideLimbDraw for the pieces the SNES game can
 * drive: wing loss swaps the broken lists, flaps follow the stick. */
static int ship_limb_override(void *ctx, int limb, int *dl, HostMeshVec3 *t,
                              HostMeshVec3 *r) {
  (void)t;
  const LimbCtx *c = (const LimbCtx *)ctx;
  switch (limb) {
  case kArwingLimb_WingA: /* decomp "right wing" list; N64 -z side */
    if (c->wing_state & 2) *dl = g_rt.list_broken_a;
    break;
  case kArwingLimb_WingB: /* decomp "left wing" list; N64 +z side */
    if (c->wing_state & 1) *dl = g_rt.list_broken_b;
    break;
  case kArwingLimb_UpperRightFlap:
    r->y -= c->flap_pitch + c->flap_roll;
    break;
  case kArwingLimb_LowerRightFlap:
    r->y -= c->flap_pitch + c->flap_roll;
    break;
  case kArwingLimb_LowerLeftFlap:
    r->y -= c->flap_pitch - c->flap_roll;
    break;
  case kArwingLimb_UpperLeftFlap:
    r->y -= c->flap_pitch - c->flap_roll;
    break;
  default:
    break;
  }
  return 1;
}

/* Build R = M^T * A where M is the Enhanced Q15 object*view matrix (points
 * transform as cam = M^T p). The posed SF64 skeleton points its nose along
 * +Z: Display_Arwing places the reticle at +Z and Display_PlayerFeatures
 * places the engine glow at -Z. Convert to the SNES object's handedness with
 * A = diag(-1,-1,1). Negating Z instead of X turned the ship backwards. */
static void ship_rotation(const int16_t m_q15[9], float out[9]) {
  const float a[3] = {-1.0f, -1.0f, 1.0f};
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      out[i * 3 + j] = ((float)m_q15[j * 3 + i] / 32768.0f) * a[j];
}

static int skip_ship_limb(void *ctx, int limb, int *dl, HostMeshVec3 *t,
                          HostMeshVec3 *r) {
  (void)ctx; (void)limb; (void)dl; (void)t; (void)r;
  return 0;
}

uint32_t arwing64_draw_shot(uint8_t *pixels, size_t pitch, int width, int height,
                          const uint8_t *rom, size_t rom_size,
                          const StarFoxEnhancedNativeShapePose *pose,
                          int kind, int transparent_black) {
  if (!arwing64_active() || !pixels || !pose || kind < 1 || kind > 2 ||
      g_rt.list_laser[kind - 1] < 0) return 0;
  int16_t matrix[9];
  if (!StarFoxEnhancedComputeShapeMatrix(rom, rom_size, pose, matrix)) return 0;
  float rotation[9];
  ship_rotation(matrix, rotation);
  /* Fit the SF64 geometry to the retail bolt's 320-unit trail. The guest
   * position remains the leading tip; no new shots or trajectories exist. */
  const float axis_scale[3] = {3.0f, 3.0f, 320.0f / 82.0f};
  HostMeshExtraList list;
  memset(&list, 0, sizeof(list));
  list.display_list = g_rt.list_laser[kind - 1];
  list.alpha_scale = 1.0f;
  const float position[3] = {(float)pose->x, (float)pose->y, (float)pose->z};
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++)
      list.model_to_camera[i * 4 + j] = rotation[i * 3 + j] * axis_scale[j];
    list.model_to_camera[i * 4 + 3] = position[i] - rotation[i * 3 + 2] * 55.0f * axis_scale[2];
  }
  Projection projection = {(float)pose->vanish_x + pose->widescreen_extra,
                           (float)pose->vanish_y};
  HostMeshDrawParams draw;
  host_mesh_draw_params_init(&draw);
  draw.mesh = g_rt.mesh;
  draw.override.fn = skip_ship_limb;
  draw.extra_lists = &list;
  draw.extra_list_count = 1;
  draw.projection.project = project_starfox;
  draw.projection.ctx = &projection;
  draw.projection.near_z = kNearZ;
  draw.target = pixels; draw.target_pitch = pitch;
  draw.target_width = width; draw.target_height = height;
  draw.supersample = g_config.arwing64_supersample;
  draw.flip_winding = 1;
  draw.transparent_black_target = transparent_black;
  return host_mesh_draw(&draw);
}

uint32_t arwing64_draw_player(uint8_t *pixels, size_t pitch, int width,
                              int height, const uint8_t *rom, size_t rom_size,
                              const StarFoxEnhancedNativeShapePose *pose,
                              int transparent_black) {
  if (!arwing64_active() || !pixels || !pose) return 0;
  const Arwing64GuestState *g = &g_rt.guest;
  if (g->cockpit) {
    g_rt.stats.frames_skipped_cockpit++;
    return 0;
  }
  int16_t m_q15[9];
  if (!StarFoxEnhancedComputeShapeMatrix(rom, rom_size, pose, m_q15)) return 0;

  Projection proj;
  proj.vanish_x = (float)pose->vanish_x + (float)pose->widescreen_extra;
  proj.vanish_y = (float)pose->vanish_y;

  float rot[9];
  ship_rotation(m_q15, rot);
  const float translation[3] = {(float)pose->x, (float)pose->y, (float)pose->z};
  int supersample = g_config.arwing64_supersample;
  if (supersample < 1) supersample = 1;
  if (supersample > 4) supersample = 4;

  HostMeshDrawParams p;
  host_mesh_draw_params_init(&p);
  p.mesh = g_rt.mesh;
  /* Wing pose: planets fly half open, space closed, the SNES game has no
   * all-range mode. */
  p.pose = (g->gamemode & 1) ? g_rt.pose_closed : g_rt.pose_half_open;
  host_mesh_matrix_compose(p.model_to_camera, rot, kModelScale, translation);
  p.projection.project = project_starfox;
  p.projection.ctx = &proj;
  p.projection.near_z = kNearZ;
  LimbCtx limb_ctx = {g->wing_state, g_rt.flap_pitch_deg, g_rt.flap_roll_deg};
  p.override.fn = ship_limb_override;
  p.override.ctx = &limb_ctx;
  p.target = pixels;
  p.target_pitch = pitch;
  p.target_width = width;
  p.target_height = height;
  p.supersample = supersample;
  /* The Star Fox projection keeps y pointing down on screen, so the mesh's
   * counter-clockwise front faces arrive clockwise: flip the winding test. */
  p.flip_winding = 1;
  /* Light from the upper left in front of the ship (camera y is down). */
  p.light_dir[0] = -0.35f;
  p.light_dir[1] = -0.75f;
  p.light_dir[2] = -0.55f;
  p.ambient = 0.45f;
  p.diffuse = 0.55f;
  if (g->flash_count && (g_rt.frame_counter & 1u)) {
    /* Damage flash: the N64 alternates a solid prim colour; approximate with
     * a strong tint. Blue when shields are healthy, red when low. */
    if (g->damage > 24) {
      p.tint[0] = 0.4f;
      p.tint[1] = 0.4f;
      p.tint[2] = 1.6f;
    } else {
      p.tint[0] = 1.6f;
      p.tint[1] = 0.3f;
      p.tint[2] = 0.3f;
    }
  }
  p.transparent_black_target = transparent_black;

  /* Detached effect quads share the ship's Z-buffer so the hull occludes
   * them correctly. */
  HostMeshExtraList extras[2];
  int extra_count = 0;
  /* Engine glow: a camera-facing quad just behind the tail (SF64 model -Z,
   * transformed by the same rotation as the hull), red on planets and blue
   * in space, sized by boost intensity and
   * flickering between two scales like the N64 game. */
  {
    const float tail = (71.0f + 10.0f) * kModelScale;
    const float boost_gain = g->boosting ? 1.15f : (g->braking ? 0.45f : 0.75f);
    float back[3];
    for (int i = 0; i < 3; i++) back[i] = translation[i] - rot[i * 3 + 2] * tail;
    const float flicker = (g_rt.frame_counter & 1u) ? 0.9f : 0.81f;
    const float s = kModelScale * flicker * boost_gain;
    const float identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    const int orb = g_rt.list_orb[(g->gamemode & 1) ? 1 : 0];
    if (orb >= 0 && back[2] > kNearZ) {
      HostMeshExtraList *x = &extras[extra_count++];
      x->display_list = orb;
      host_mesh_matrix_compose(x->model_to_camera, identity, s, back);
      x->alpha_scale = g->boosting ? 1.0f : 0.85f;
      g_rt.stats.glow_draws++;
    }
  }
  /* Barrel roll shield: spins about the view axis, fades with the roll. */
  if (g->rolling && g_rt.list_shield >= 0) {
    const float dir = g->roll_velocity < 0 ? -1.0f : 1.0f;
    const float ang = (float)g_rt.frame_counter * 20.0f * dir * 3.14159265f / 180.0f;
    const float c = cosf(ang), sn = sinf(ang);
    const float spin[9] = {c, -sn, 0, sn, c, 0, 0, 0, 1};
    HostMeshExtraList *x = &extras[extra_count++];
    x->display_list = g_rt.list_shield;
    host_mesh_matrix_compose(x->model_to_camera, spin, 2.0f * kModelScale,
                             translation);
    x->alpha_scale = fminf(1.0f, fabsf((float)g->roll_velocity) / 32.0f);
    g_rt.stats.shield_draws++;
  }
  p.extra_lists = extras;
  p.extra_list_count = extra_count;
  HostMeshDrawStats stats;
  p.stats = &stats;
  uint32_t written = host_mesh_draw(&p);
  g_rt.stats.frames_drawn++;
  g_rt.stats.last_pixels = written;
  g_rt.stats.last_triangles = stats.triangles_rasterised;
  g_rt.stats.last_bbox[0] = stats.bbox_min_x;
  g_rt.stats.last_bbox[1] = stats.bbox_min_y;
  g_rt.stats.last_bbox[2] = stats.bbox_max_x;
  g_rt.stats.last_bbox[3] = stats.bbox_max_y;
  return written;
}

const Arwing64Stats *arwing64_stats(void) { return &g_rt.stats; }

/* ---- debug ------------------------------------------------------------------ */

int arwing64_debug_command(const char *args, Arwing64SendLine send_line) {
  char line[512];
  const Arwing64Stats *s = &g_rt.stats;
  if (!args || !*args || strcmp(args, "status") == 0) {
    snprintf(line, sizeof(line),
             "arwing64 status=%s detail=%s enabled=%d guest_patch=0 frames=%u "
             "cockpit_skips=%u last_pixels=%u last_tris=%u bbox=%d,%d..%d,%d "
             "glow=%u shield=%u",
             arwing64_status_name(s->status),
             s->status_detail ? s->status_detail : "-",
             g_config.arwing64_enabled ? 1 : 0,
             s->frames_drawn, s->frames_skipped_cockpit, s->last_pixels,
             s->last_triangles, s->last_bbox[0], s->last_bbox[1],
             s->last_bbox[2], s->last_bbox[3], s->glow_draws, s->shield_draws);
    send_line(line);
    return 1;
  }
  if (strcmp(args, "state") == 0) {
    const Arwing64GuestState *g = &g_rt.guest;
    snprintf(line, sizeof(line),
             "arwing64 guest wing=%u psf=%02x psf2=%02x psf3=%02x view=%u "
             "gameflags=%02x/%02x gamemode=%02x roll_vel=%d roll_off=%d "
             "flash=%u/%u boost_anim=%u boost_charge=%u damage=%u obj=%04x "
             "rot=%u,%u,%u dpitch=%d flaps=%.1f/%.1f",
             g->wing_state, g->pshipflags, g->pshipflags2, g->pshipflags3,
             g->view_mode, g->gameflags, g->gameflags2, g->gamemode,
             g->roll_velocity, g->roll_offset, g->flash_count, g->flash_type,
             g->boost_anim, g->boost_charge, g->damage, g->player_object,
             g->player_pitch, g->player_yaw, g->player_roll, g->pitch_delta,
             g_rt.flap_pitch_deg, g_rt.flap_roll_deg);
    send_line(line);
    return 1;
  }
  if (strcmp(args, "cache") == 0) {
    snprintf(line, sizeof(line), "arwing64 cache=%s sha256=%s lists=%u limbs=%u",
             s->cache_path[0] ? s->cache_path : "-",
             s->blob_sha256[0] ? s->blob_sha256 : "-",
             g_rt.mesh ? g_rt.mesh->display_list_count : 0u,
             g_rt.mesh ? g_rt.mesh->limb_count : 0u);
    send_line(line);
    return 1;
  }
  if (strcmp(args, "patch") == 0 || strcmp(args, "picture") == 0) {
    arwing64_picture_debug(send_line);
    return 1;
  }
  if (strcmp(args, "audio") == 0) {
    const Arwing64AudioStats *a = arwing64_audio_stats();
    snprintf(line, sizeof(line),
             "arwing64 audio enabled=%d clips=%d missing=%d played=%u "
             "consumed=%u passed=%u last_id=%02x engine_byte=%02x loop=%d dir=%s",
             a->enabled, a->clips_loaded, a->clips_missing, a->cues_played,
             a->snes_sfx_consumed, a->snes_sfx_passed, a->last_snes_id,
             a->last_engine_byte, a->engine_loop_active,
             a->cache_audio_dir[0] ? a->cache_audio_dir : "-");
    send_line(line);
    return 1;
  }
  if (strncmp(args, "force", 5) == 0) {
    /* arwing force [wing=N] [roll=N] [boost=0|1] [brake=0|1] [flash=N] |
     * arwing force clear -- presentation-only overrides for capturing the
     * damage / shield / boost visuals without driving the guest. */
    const char *p = args + 5;
    if (strstr(p, "clear")) reset_overrides();
    const char *tok;
    if ((tok = strstr(p, "wing="))) g_rt.force_wing = atoi(tok + 5);
    if ((tok = strstr(p, "roll="))) g_rt.force_roll = atoi(tok + 5);
    if ((tok = strstr(p, "boost="))) g_rt.force_boost = atoi(tok + 6);
    if ((tok = strstr(p, "brake="))) g_rt.force_brake = atoi(tok + 6);
    if ((tok = strstr(p, "flash="))) g_rt.force_flash = atoi(tok + 6);
    snprintf(line, sizeof(line),
             "arwing64 force wing=%d roll=%d boost=%d brake=%d flash=%d",
             g_rt.force_wing, g_rt.force_roll, g_rt.force_boost,
             g_rt.force_brake, g_rt.force_flash);
    send_line(line);
    return 1;
  }
  send_line("arwing64: usage arwing [status|state|cache|patch|force ...]");
  return 1;
}
