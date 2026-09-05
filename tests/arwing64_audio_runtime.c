/* Isolated cue protocol + mixer integration. Synthetic WAVs are made by Python. */
#include "arwing64_audio.h"
#include "common_rtl.h"
#include "mod_audio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static RtlApuPortObserver write_observer;
static RtlApuPortReadObserver read_observer;
void RtlApuLock(void) {}
void RtlApuUnlock(void) {}
int RtlAddApuPortObserver(RtlApuPortObserver cb) { write_observer=cb; return 1; }
void RtlRemoveApuPortObserver(RtlApuPortObserver cb) { (void)cb; write_observer=NULL; }
int RtlAddApuPortReadObserver(RtlApuPortReadObserver cb) { read_observer=cb; return 1; }
/* Cache hashing is covered separately with the owner ROM. This test isolates
 * WAV parsing, host mixer ownership and the guest acknowledgement protocol. */
int sf64_audio_cache_valid(const char *dir) { (void)dir; return 1; }
int main(int argc, char **argv) {
  assert(argc==2);
  assert(arwing64_audio_refresh(argv[1],1));
  assert(arwing64_audio_stats()->clips_loaded==kArwingCue_Count);
  assert(!write_observer(0x2143,0x35)); /* no interception during IPL/menus */
  arwing64_audio_frame(0,1);
  const uint8_t ids[]={0x35,0x34,0x36,0x31,0x30,0x32,0x33,0x07,0x08,0x05,0x06,0x04,0x19,0x03,0x14};
  int16_t mix[2000];
  for (unsigned i=0;i<sizeof(ids);i++) {
    unsigned before=arwing64_audio_stats()->cues_played;
    assert(write_observer(0x217f,ids[i]));
    assert(write_observer(0x2143,ids[i]));
    assert(arwing64_audio_stats()->cues_played==before+1);
    uint8_t ack=0; assert(read_observer(0x2143,&ack) && ack==ids[i]);
    assert(!write_observer(0x2143,0));
    assert(!read_observer(0x2143,&ack));
    memset(mix,0,sizeof(mix)); snes_mod_audio_mix(mix,1000,32040,2);
    int heard=0; for (unsigned n=0;n<2000;n++) heard|=mix[n]; assert(heard);
  }
  assert(arwing64_audio_stats()->snes_sfx_consumed==sizeof(ids));
  assert(!write_observer(0x2143,0x1b)); /* stock shield alarm */
  assert(!write_observer(0x2140,0x35)); /* IPL/music */
  assert(write_observer(0x2141,0xc4)); arwing64_audio_frame(0,1);
  assert(arwing64_audio_stats()->engine_loop_active);
  assert(!write_observer(0x2141,0x4b)); arwing64_audio_frame(0,1);
  assert(arwing64_audio_stats()->engine_loop_active);
  unsigned before=arwing64_audio_stats()->cues_played;
  arwing64_audio_frame(1,1); arwing64_audio_frame(1,1);
  assert(arwing64_audio_stats()->cues_played==before+1);
  arwing64_audio_frame(0,1); arwing64_audio_frame(1,1);
  assert(arwing64_audio_stats()->cues_played==before+2);
  snes_mod_audio_stop_all(); arwing64_audio_frame(0,1);
  assert(!arwing64_audio_stats()->engine_loop_active);
  assert(write_observer(0x2141,0xc4)); arwing64_audio_frame(0,1);
  assert(arwing64_audio_stats()->engine_loop_active);
  arwing64_audio_frame(0,0);
  assert(!arwing64_audio_stats()->engine_loop_active);
  assert(!write_observer(0x2141,0xc4));
  assert(!arwing64_audio_stats()->engine_loop_active);
  assert(arwing64_audio_refresh(NULL,0));
  assert(!write_observer(0x2143,0x35));
  assert(!arwing64_audio_stats()->clips_loaded);
  assert(arwing64_audio_refresh(argv[1],1));
  assert(arwing64_audio_refresh(NULL,0));
  puts("Arwing64 cue, acknowledgement, loop and lifecycle tests passed");
  return 0;
}
