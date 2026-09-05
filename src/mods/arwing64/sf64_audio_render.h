/* Bounded, owner-ROM-only SF64 player SFX extraction. No asset data is baked in. */
#pragma once
#include "sf64_rom.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SF64_AUDIO_RATE 32040u
#define SF64_AUDIO_CUES 14
const char *sf64_audio_filename(unsigned cue);
/* Verify all WAVs against the versioned SHA-256 manifest. */
int sf64_audio_cache_valid(const char *cache_dir);
/* Write <cache_dir>/audio; commit the manifest only after every WAV succeeds. */
int sf64_audio_extract(const Sf64Rom *rom, const char *cache_dir, const char **error);
/* Exposed for a differential test against the decomp's my_decodeframe. */
int sf64_audio_decode_sample(const Sf64Rom *rom, unsigned instrument,
                             int16_t **pcm, uint32_t *frames, const char **error);
#ifdef __cplusplus
}
#endif
