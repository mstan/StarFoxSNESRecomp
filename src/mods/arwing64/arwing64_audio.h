/* Arwing64 audio: replace the SNES Arwing sound effects with Star Fox 64's.
 *
 * The SNES game requests one sound effect per byte written to APU port
 * $2143 (docs/ARWING64_SOURCE_SEAMS.md). An APU port observer maps the IDs
 * for the player's ship (laser, twin laser, nova bomb, boost, brake, wing
 * hits and loss, ship explosion) onto SF64 clips mixed by the engine's
 * mod_audio overlay, and consumes the write so the SPC stays silent for
 * exactly those IDs. The engine hum on $2141 drives a looping clip whose
 * pitch follows boost/brake. Everything else the SPC plays is untouched.
 *
 * Clips come from the Arwing64 cache (audio/<cue>.wav, 16-bit PCM) written
 * by the extractor from the user's own SF64 ROM; a missing or invalid clip
 * disables only that cue.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum Arwing64Cue {
  kArwingCue_Laser = 0,
  kArwingCue_TwinLaser,
  kArwingCue_BeamLaser,
  kArwingCue_BombShot,
  kArwingCue_BombExplode,
  kArwingCue_Boost,
  kArwingCue_Brake,
  kArwingCue_WingHit,
  kArwingCue_WingLost,
  kArwingCue_BodyHit,
  kArwingCue_Explosion,
  kArwingCue_ShieldDeflect,
  kArwingCue_Engine, /* looping */
  kArwingCue_Count,
} Arwing64Cue;

typedef struct Arwing64AudioStats {
  int enabled;
  int clips_loaded;
  int clips_missing;
  uint32_t cues_played;
  uint32_t snes_sfx_consumed;
  uint32_t snes_sfx_passed;
  uint8_t last_snes_id;
  uint8_t last_engine_byte;
  int engine_loop_active;
  char cache_audio_dir[512];
} Arwing64AudioStats;

/* Load clips from <cache_dir>/audio and install the APU observer. Safe to
 * call repeatedly; re-reads only when the directory changes. */
void arwing64_audio_refresh(const char *cache_dir, int enabled);

/* Per presentation frame: keeps the engine loop in step with the guest. */
void arwing64_audio_frame(void);

const Arwing64AudioStats *arwing64_audio_stats(void);
const char *arwing64_cue_name(Arwing64Cue cue);

#ifdef __cplusplus
}
#endif
