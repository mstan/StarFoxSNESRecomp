#include "arwing64_audio.h"

#include "common_rtl.h"
#include "mod_audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SNES sound-effect IDs on APU port $2143 (retail call sites listed in
 * docs/ARWING64_SOURCE_SEAMS.md). */
enum {
  kSnesSfx_PlayerDown = 0x03,
  kSnesSfx_PlayerDamage = 0x04,
  kSnesSfx_WingDestructLeft = 0x05,
  kSnesSfx_WingDestructRight = 0x06,
  kSnesSfx_WingDamageLeft = 0x07,
  kSnesSfx_WingDamageRight = 0x08,
  kSnesSfx_ShieldDeflect = 0x14,
  kSnesSfx_PlayerDamageLight = 0x19,
  kSnesSfx_BombExplode = 0x30,
  kSnesSfx_BombShot = 0x31,
  kSnesSfx_Boost = 0x32,
  kSnesSfx_Brake = 0x33,
  kSnesSfx_TwinLaser = 0x34,
  kSnesSfx_Laser = 0x35,
  kSnesSfx_BeamLaser = 0x36,
};

typedef struct CueMap {
  uint8_t snes_id;
  Arwing64Cue cue;
} CueMap;

static const CueMap kCueMap[] = {
    {kSnesSfx_Laser, kArwingCue_Laser},
    {kSnesSfx_TwinLaser, kArwingCue_TwinLaser},
    {kSnesSfx_BeamLaser, kArwingCue_BeamLaser},
    {kSnesSfx_BombShot, kArwingCue_BombShot},
    {kSnesSfx_BombExplode, kArwingCue_BombExplode},
    {kSnesSfx_Boost, kArwingCue_Boost},
    {kSnesSfx_Brake, kArwingCue_Brake},
    {kSnesSfx_WingDamageLeft, kArwingCue_WingHit},
    {kSnesSfx_WingDamageRight, kArwingCue_WingHit},
    {kSnesSfx_WingDestructLeft, kArwingCue_WingLost},
    {kSnesSfx_WingDestructRight, kArwingCue_WingLost},
    {kSnesSfx_PlayerDamage, kArwingCue_BodyHit},
    {kSnesSfx_PlayerDamageLight, kArwingCue_BodyHit},
    {kSnesSfx_PlayerDown, kArwingCue_Explosion},
    {kSnesSfx_ShieldDeflect, kArwingCue_ShieldDeflect},
};

static const char *const kCueFiles[kArwingCue_Count] = {
    "sfx_laser.wav",       "sfx_twin_laser.wav",   "sfx_beam_laser.wav",
    "sfx_bomb_shot.wav",   "sfx_bomb_explode.wav", "sfx_boost.wav",
    "sfx_brake.wav",       "sfx_wing_hit.wav",     "sfx_wing_lost.wav",
    "sfx_body_hit.wav",    "sfx_explosion.wav",    "sfx_shield_deflect.wav",
    "sfx_engine_loop.wav",
};

typedef struct AudioRuntime {
  int installed;
  int enabled;
  char dir[512];
  SNESModAudioClip clips[kArwingCue_Count];
  SNESModAudioVoice engine_voice;
  uint8_t engine_byte;
  int engine_pitch_q10;
  Arwing64AudioStats stats;
} AudioRuntime;

static AudioRuntime g_audio;

const char *arwing64_cue_name(Arwing64Cue cue) {
  return cue >= 0 && cue < kArwingCue_Count ? kCueFiles[cue] : "?";
}

/* ---- WAV loading ------------------------------------------------------------ */

static uint32_t rd32(const uint8_t *p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

/* Strict RIFF/WAVE PCM-16 loader (mono or stereo, 8-192 kHz, <= 16 MiB). */
static SNESModAudioClip load_wav(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return SNES_MOD_AUDIO_CLIP_INVALID;
  fseek(f, 0, SEEK_END);
  const long len = ftell(f);
  rewind(f);
  if (len < 44 || len > (16L << 20)) {
    fclose(f);
    return SNES_MOD_AUDIO_CLIP_INVALID;
  }
  uint8_t *d = (uint8_t *)malloc((size_t)len);
  if (!d || fread(d, 1, (size_t)len, f) != (size_t)len) {
    free(d);
    fclose(f);
    return SNES_MOD_AUDIO_CLIP_INVALID;
  }
  fclose(f);
  SNESModAudioClip clip = SNES_MOD_AUDIO_CLIP_INVALID;
  if (memcmp(d, "RIFF", 4) != 0 || memcmp(d + 8, "WAVE", 4) != 0) goto done;
  uint32_t pos = 12, rate = 0, channels = 0, bits = 0, fmt = 0;
  const uint8_t *data = NULL;
  uint32_t data_len = 0;
  while (pos + 8 <= (uint32_t)len) {
    const uint32_t chunk_len = rd32(d + pos + 4);
    const uint8_t *body = d + pos + 8;
    if (pos + 8 + chunk_len > (uint32_t)len) goto done;
    if (memcmp(d + pos, "fmt ", 4) == 0) {
      if (chunk_len < 16) goto done;
      fmt = rd16(body);
      channels = rd16(body + 2);
      rate = rd32(body + 4);
      bits = rd16(body + 14);
    } else if (memcmp(d + pos, "data", 4) == 0) {
      data = body;
      data_len = chunk_len;
    }
    pos += 8 + chunk_len + (chunk_len & 1u);
  }
  if (fmt != 1 || bits != 16 || (channels != 1 && channels != 2) || !data ||
      data_len < 4 || rate < 8000 || rate > 192000)
    goto done;
  const uint32_t frames = data_len / (2u * channels);
  int16_t *pcm = (int16_t *)malloc((size_t)frames * channels * 2u);
  if (!pcm) goto done;
  for (uint32_t i = 0; i < frames * channels; i++)
    pcm[i] = (int16_t)rd16(data + i * 2u);
  clip = snes_mod_audio_register_pcm_s16(pcm, frames, rate, channels);
  free(pcm);
done:
  free(d);
  return clip;
}

static void unload_all(void) {
  if (g_audio.engine_voice) {
    snes_mod_audio_stop_voice(g_audio.engine_voice);
    g_audio.engine_voice = SNES_MOD_AUDIO_VOICE_INVALID;
  }
  for (int i = 0; i < kArwingCue_Count; i++) {
    if (g_audio.clips[i]) snes_mod_audio_unregister(g_audio.clips[i]);
    g_audio.clips[i] = SNES_MOD_AUDIO_CLIP_INVALID;
  }
  g_audio.stats.clips_loaded = 0;
  g_audio.stats.clips_missing = 0;
}

/* ---- APU port observer ------------------------------------------------------ */

static int play_cue(Arwing64Cue cue) {
  if (cue < 0 || cue >= kArwingCue_Count || !g_audio.clips[cue]) return 0;
  if (!snes_mod_audio_play(g_audio.clips[cue], 100)) return 0;
  g_audio.stats.cues_played++;
  return 1;
}

static int apu_port_observer(uint16 reg, uint8 value) {
  if (!g_audio.enabled) return 0;
  if (reg == 0x2141) {
    /* Engine byte, one write per frame; $00 = off. Handled in the frame
     * tick so the loop starts/stops smoothly; never consumed (the SPC keeps
     * its own engine layer, which is part of the SNES mix the user expects
     * under the SF64 clips only when no SF64 engine clip is present). */
    g_audio.engine_byte = value;
    g_audio.stats.last_engine_byte = value;
    return g_audio.clips[kArwingCue_Engine] != SNES_MOD_AUDIO_CLIP_INVALID;
  }
  if (reg != 0x2143) return 0;
  if (value == 0) return 0; /* handshake clear */
  g_audio.stats.last_snes_id = value;
  for (size_t i = 0; i < sizeof(kCueMap) / sizeof(kCueMap[0]); i++) {
    if (kCueMap[i].snes_id != value) continue;
    if (play_cue(kCueMap[i].cue)) {
      g_audio.stats.snes_sfx_consumed++;
      return 1; /* replaced: keep the SPC from playing the SNES version */
    }
    break;
  }
  g_audio.stats.snes_sfx_passed++;
  return 0;
}

/* ---- lifecycle -------------------------------------------------------------- */

void arwing64_audio_refresh(const char *cache_dir, int enabled) {
  if (!g_audio.installed) {
    RtlAddApuPortObserver(apu_port_observer);
    g_audio.installed = 1;
  }
  g_audio.enabled = enabled ? 1 : 0;
  g_audio.stats.enabled = g_audio.enabled;
  if (!enabled || !cache_dir || !cache_dir[0]) {
    unload_all();
    g_audio.dir[0] = 0;
    return;
  }
  if (strcmp(g_audio.dir, cache_dir) == 0 && g_audio.stats.clips_loaded > 0) return;
  unload_all();
  snprintf(g_audio.dir, sizeof(g_audio.dir), "%s", cache_dir);
  snprintf(g_audio.stats.cache_audio_dir, sizeof(g_audio.stats.cache_audio_dir),
           "%s/audio", cache_dir);
  for (int i = 0; i < kArwingCue_Count; i++) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/audio/%s", cache_dir, kCueFiles[i]);
    g_audio.clips[i] = load_wav(path);
    if (g_audio.clips[i]) g_audio.stats.clips_loaded++;
    else g_audio.stats.clips_missing++;
  }
}

void arwing64_audio_frame(void) {
  if (!g_audio.enabled) return;
  const SNESModAudioClip engine = g_audio.clips[kArwingCue_Engine];
  if (!engine) return;
  const uint8_t b = g_audio.engine_byte;
  const int want = b != 0;
  if (want && !g_audio.engine_voice) {
    g_audio.engine_voice = snes_mod_audio_play_loop(engine, 70, 0, 0);
    g_audio.stats.engine_loop_active = g_audio.engine_voice != 0;
  } else if (!want && g_audio.engine_voice) {
    snes_mod_audio_stop_voice(g_audio.engine_voice);
    g_audio.engine_voice = SNES_MOD_AUDIO_VOICE_INVALID;
    g_audio.stats.engine_loop_active = 0;
  }
  if (g_audio.engine_voice) {
    /* $4B = low-shield alarm (leave pitch alone); else bits 2-3 of the flag
     * nibble: $04 normal, $08 boosting, $0C braking. */
    int target = 1024;
    if (b != 0x4b) {
      const int mode = (b >> 2) & 3;
      if (mode == 2) target = 1400;
      else if (mode == 3) target = 800;
    }
    if (target != g_audio.engine_pitch_q10) {
      /* Glide toward the target so boost/brake sweep instead of stepping. */
      const int step = (target - g_audio.engine_pitch_q10) / 4;
      g_audio.engine_pitch_q10 += step == 0 ? (target > g_audio.engine_pitch_q10 ? 1 : -1) : step;
      snes_mod_audio_set_voice_pitch(g_audio.engine_voice, g_audio.engine_pitch_q10);
    }
    if (!snes_mod_audio_voice_active(g_audio.engine_voice)) {
      g_audio.engine_voice = SNES_MOD_AUDIO_VOICE_INVALID;
      g_audio.stats.engine_loop_active = 0;
    }
  }
}

const Arwing64AudioStats *arwing64_audio_stats(void) { return &g_audio.stats; }
