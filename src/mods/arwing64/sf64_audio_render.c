/* Dry SF64 player-bank renderer. Script/ADSR/portamento semantics were checked
 * against sonicdcer/sf64 (CC0), src/audio/audio_{seqplayer,effects}.c and
 * tools/aifc_decode.c. This is deliberately not a general N64 audio emulator:
 * bounded player-bank scripts, sampled instruments, no spatialisation/reverb.
 * All tables, envelopes, predictor books and samples come from the owner's ROM.
 */
#include "sf64_audio_render.h"
#include "sha256.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define make_dir(p) mkdir(p, 0755)
#endif

enum { MAX_FRAMES = SF64_AUDIO_RATE * 8, MAX_EVENTS = 128, MAX_STEPS = 4096 };
static const struct { const char *file; unsigned id; } kCues[] = {
  {"sfx_laser.wav", 0x00}, {"sfx_twin_laser.wav", 0x0c},
  {"sfx_beam_laser.wav", 0x2b}, {"sfx_bomb_shot.wav", 0x01},
  {"sfx_bomb_explode.wav", 0x0a}, {"sfx_boost.wav", 0x02},
  {"sfx_brake.wav", 0x03}, {"sfx_wing_hit.wav", 0x0e},
  {"sfx_wing_lost.wav", 0x2c}, {"sfx_body_hit.wav", 0x0f},
  {"sfx_explosion.wav", 0x04}, {"sfx_shield_deflect.wav", 0x11},
  {"sfx_engine_loop.wav", 0x05}, {"sfx_roll.wav", 0x12},
};
const char *sf64_audio_filename(unsigned cue) {
  return cue < SF64_AUDIO_CUES ? kCues[cue].file : NULL;
}
typedef struct Bytes { const uint8_t *p; size_t n; } Bytes;
typedef struct Bank { Bytes seq, font, samples; } Bank;
static int span(Bytes b, size_t p, size_t n) { return p <= b.n && n <= b.n - p; }
static int fail(const char **e, const char *s) { if (e) *e = s; return 0; }
static int bank_open(const Sf64Rom *rom, Bank *b) {
  if (!rom || !rom->data || strcmp(rom->sha1_hex, SF64_US11_SHA1)) return 0;
  Bytes r = {rom->data, rom->size};
  /* US v1.1 naudio tables; font 0, sample bank 0, sequence 0. */
  if (!span(r, 0xc4690, 32) || !span(r, 0xc4210, 32)) return 0;
  Sf64FileEntry seq, font, samples;
  if (!sf64_rom_file_entry(rom, kSf64File_AudioSeq, &seq) ||
      !sf64_rom_file_entry(rom, kSf64File_AudioBank, &font) ||
      !sf64_rom_file_entry(rom, kSf64File_AudioTable, &samples) ||
      seq.compressed || font.compressed || samples.compressed) return 0;
  size_t fo = font.rom_start + sf64_be32(r.p + 0xc46a0);
  size_t fn = sf64_be32(r.p + 0xc46a4);
  size_t so = samples.rom_start + sf64_be32(r.p + 0xc4220);
  size_t sn = sf64_be32(r.p + 0xc4224);
  if (!span(r, fo, fn) || !span(r, so, sn) || !span(r, seq.rom_start, 0x3af0)) return 0;
  b->font = (Bytes){r.p + fo, fn}; b->samples = (Bytes){r.p + so, sn};
  b->seq = (Bytes){r.p + seq.rom_start, 0x3af0};
  return fn >= 0x200;
}
typedef struct Sample {
  int16_t *pcm, *loop_pcm;
  uint32_t frames, start, end, count, address;
  float tuning;
  unsigned envelope, decay;
} Sample;
static int16_t clamp16(int64_t v) { return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v); }
static void sample_free(Sample *s) { free(s->pcm); free(s->loop_pcm); memset(s, 0, sizeof(*s)); }
/* Each predictor is order columns of eight coefficients. The remaining
 * columns are shifted copies of the last column. Keep unclipped history as
 * the reference decoder does; saturate only the PCM output. */
static int decode_frames(Bytes data, Bytes book, uint32_t first, uint32_t last,
                         int32_t state[16], int16_t *out) {
  if (!span(book, 0, 8)) return 0;
  unsigned order = sf64_be32(book.p), predictors = sf64_be32(book.p + 4);
  if (!order || order > 8 || !predictors || predictors > 16 ||
      !span(book, 8, order * predictors * 16) || !span(data, first * 9u, (last - first) * 9u)) return 0;
  for (uint32_t f = first; f < last; f++) {
    const uint8_t *d = data.p + f * 9;
    unsigned predictor = d[0] & 15, scale = d[0] >> 4;
    if (predictor >= predictors) return 0;
    const uint8_t *c = book.p + 8 + predictor * order * 16;
    int32_t residual[16];
    for (unsigned i = 0; i < 16; i++) {
      int v = (d[1+i/2] >> ((i & 1) ? 0 : 4)) & 15;
      residual[i] = (v >= 8 ? v - 16 : v) * (1 << scale);
    }
    for (unsigned h = 0; h < 2; h++) {
      int32_t history[8];
      for (unsigned j = 0; j < order; j++) history[j] = state[(h ? 8 : 16) - order + j];
      for (unsigned i = 0; i < 8; i++) {
        int64_t v = 0;
        for (unsigned j = 0; j < order; j++) v += (int64_t)sf64_bes16(c + (j*8+i)*2) * history[j];
        for (unsigned j = 0; j < i; j++) v += (int64_t)sf64_bes16(c + ((order-1)*8+i-j-1)*2) * residual[h*8+j];
        v = (v >= 0 ? v / 2048 : -((-v + 2047) / 2048)) + residual[h*8+i];
        if (v < INT32_MIN || v > INT32_MAX) return 0;
        state[h*8+i] = (int32_t)v;
        out[(f-first)*16+h*8+i] = clamp16(v);
      }
    }
  }
  return 1;
}
static int sample_load(Bank *b, unsigned instrument, unsigned note, Sample *s) {
  memset(s, 0, sizeof(*s));
  if (instrument >= 127 || !span(b->font, 4+instrument*4, 4)) return 0;
  unsigned ip = sf64_be32(b->font.p + 4+instrument*4);
  if (!ip || !span(b->font, ip, 32)) return 0;
  const uint8_t *ins = b->font.p + ip;
  unsigned ts = note < ins[1] ? 8 : note > ins[2] ? 24 : 16;
  unsigned sp = sf64_be32(ins + ts);
  s->tuning = sf64_bef32(ins + ts+4); s->envelope = sf64_be32(ins+4); s->decay = ins[3];
  if (!sp || !span(b->font, sp, 16) || !isfinite(s->tuning) || s->tuning <= 0 || s->tuning > 16) return 0;
  const uint8_t *sh = b->font.p + sp;
  unsigned size = sf64_be32(sh) & 0xffffff, offset = sf64_be32(sh+4);
  unsigned lp = sf64_be32(sh+8), bp = sf64_be32(sh+12);
  if ((sh[0] >> 4) != 0 || !span(b->samples, offset, size) || !span(b->font, lp, 16) || !span(b->font, bp, 8)) return 0;
  s->start = sf64_be32(b->font.p+lp); s->end = sf64_be32(b->font.p+lp+4);
  s->count = sf64_be32(b->font.p+lp+8); s->frames = s->end; s->address = sp;
  /* Encoded buffers are sometimes padded beyond the final complete frame. */
  unsigned nframes = (s->end + 15)/16;
  if (!s->end || s->end > 1000000 || nframes*9 > size+15 ||
      !span(b->samples, offset, nframes*9) || s->start >= s->end) return 0;
  s->pcm = malloc(nframes * 16 * sizeof(int16_t));
  Bytes data = {b->samples.p+offset, nframes*9}, book = {b->font.p+bp, b->font.n-bp};
  int32_t state[16] = {0};
  if (!s->pcm || !decode_frames(data, book, 0, nframes, state, s->pcm)) goto bad;
  if (s->count) {
    if (!span(b->font, lp+16, 32)) goto bad;
    for (unsigned i = 0; i < 16; i++) state[i] = sf64_bes16(b->font.p+lp+16+i*2);
    unsigned first = s->start/16;
    s->loop_pcm = malloc((nframes-first)*16*sizeof(int16_t));
    if (!s->loop_pcm || !decode_frames(data, book, first, nframes, state, s->loop_pcm)) goto bad;
  }
  return 1;
bad: sample_free(s); return 0;
}
int sf64_audio_decode_sample(const Sf64Rom *rom, unsigned instrument,
                             int16_t **pcm, uint32_t *frames, const char **error) {
  Bank b; Sample s;
  if (!pcm || !frames) return fail(error, "invalid output");
  *pcm = NULL; *frames = 0;
  if (!bank_open(rom, &b) || !sample_load(&b, instrument, 39, &s)) return fail(error, "invalid SF64 sample");
  *pcm = s.pcm; *frames = s.frames; free(s.loop_pcm); return 1;
}

typedef struct Script { Bytes data; unsigned pc, stack[8], loops[8], depth, steps; int bad; } Script;
static unsigned u8(Script *s) {
  if (!span(s->data, s->pc, 1)) { s->bad = 1; return 0; }
  return s->data.p[s->pc++];
}
static unsigned u16(Script *s) { unsigned a = u8(s); return a*256+u8(s); }
static unsigned var(Script *s) { unsigned a = u8(s); return a & 128 ? ((a & 127)*256+u8(s)) : a; }
/* Return 1 if handled, 2 on script end. */
static int flow(Script *s, unsigned cmd) {
  if (cmd == 0xff) { if (!s->depth) return 2; s->pc = s->stack[--s->depth]; }
  else if (cmd == 0xfb) s->pc = u16(s);
  else if (cmd == 0xf4) { int offset = (int8_t)u8(s); s->pc += offset; }
  else if (cmd == 0xfc || cmd == 0xf8) {
    unsigned arg = cmd == 0xfc ? u16(s) : u8(s);
    if (s->depth >= 8) { s->bad = 1; return 1; }
    s->stack[s->depth] = s->pc; s->loops[s->depth++] = cmd == 0xfc ? 0 : arg;
    if (cmd == 0xfc) s->pc = arg;
  } else if (cmd == 0xf7) {
    if (!s->depth || !s->loops[s->depth-1]) s->bad = 1;
    else if (--s->loops[s->depth-1]) s->pc = s->stack[s->depth-1]; else s->depth--;
  } else return 0;
  return 1;
}
typedef struct Settings { unsigned instrument, env, decay, pan; int transpose, seq_env; float volume; } Settings;
typedef struct Event {
  Settings settings;
  unsigned layer, note, target, mode, porta_time, velocity, gate, delay;
  int continuous;
  double tick;
} Event;
typedef struct Score { Event events[MAX_EVENTS]; unsigned count; int engine; } Score;
static int layer_parse(Bank *b, unsigned pc, unsigned layer, Settings settings, Score *score) {
  Script s = {.data=b->seq, .pc=pc};
  unsigned last_delay = 0, mode = 0, target = 0, time = 0;
  int transpose = 0, continuous = 0; double tick = 0;
  while (!s.bad && s.steps++ < MAX_STEPS && tick < 96*8) {
    unsigned cmd = u8(&s);
    int f = flow(&s, cmd); if (f == 2) return 1; if (f) continue;
    if (cmd < 0xc0) {
      unsigned delay = (cmd & 0xc0) == 0x80 ? last_delay : var(&s);
      unsigned vel = u8(&s), gate = (cmd & 0xc0) == 0x40 ? 0 : u8(&s);
      int note = (cmd & 63) + settings.transpose + transpose;
      if (!delay || note < 0 || note > 127 || score->count >= MAX_EVENTS) return 0;
      Event e = {settings, layer, (unsigned)note, target, mode, time, vel > 127 ? 127 : vel, gate, delay, continuous, tick};
      score->events[score->count++] = e;
      last_delay = delay; tick += delay;
      if ((mode & 127) == 5) target = note;
      if ((mode & 127) == 1 || (mode & 127) == 2) mode = 0;
      if (score->engine) return !s.bad;
    } else switch (cmd) {
      case 0xc0: tick += var(&s); continuous = 0; break;
      case 0xc2: transpose = (int8_t)u8(&s); break;
      case 0xc4: continuous = 1; break;
      case 0xc5: continuous = 0; break;
      case 0xc6: settings.instrument = u8(&s); settings.env = 0; settings.seq_env = 0; settings.decay = 0; break;
      case 0xc7: mode = u8(&s); target = (u8(&s)+settings.transpose+transpose)&127; time = mode & 128 ? u8(&s) : var(&s); if (!time) s.bad = 1; break;
      case 0xc8: mode = 0; break;
      case 0xca: settings.pan = u8(&s); break;
      case 0xcb: settings.env = u16(&s); settings.seq_env = 1; settings.decay = u8(&s); break;
      default: return 0;
    }
  }
  /* A long note/loop can run until its owning SNES request ends. Offline
   * one-shots have a documented eight-second cap and a release tail. */
  return !s.bad && tick >= 96*8;
}
static int score_parse(Bank *b, unsigned id, Score *score) {
  if (!span(b->seq, 0x105+id*2, 2)) return 0;
  Script s = {.data=b->seq, .pc=sf64_be16(b->seq.p+0x105+id*2)};
  Settings settings = {.pan=64, .volume=1.0f}; int value = 0;
  while (!s.bad && s.steps++ < MAX_STEPS) {
    unsigned cmd = u8(&s); int f = flow(&s, cmd);
    if (f == 2) return score->count > 0;
    if (f) continue;
    if (cmd >= 0x90 && cmd <= 0x92) {
      unsigned pc = u16(&s);
      if (!layer_parse(b, pc, cmd-0x90, settings, score)) return 0;
    } else switch (cmd) {
      case 0xc1: settings.instrument = u8(&s); settings.env = 0; settings.seq_env = 0; settings.decay = 0; break;
      case 0xda: settings.env = u16(&s); settings.seq_env = 1; break;
      case 0xd9: settings.decay = u8(&s); break;
      case 0xdb: settings.transpose = (int8_t)u8(&s); break;
      case 0xdd: settings.pan = u8(&s); break;
      case 0xdc: case 0xd4: u8(&s); break; /* pan weight / reverb send */
      case 0xdf: settings.volume = u8(&s)/127.0f; break;
      case 0x81: value = 0; break; /* player IO1 = planet (space variants are a later option) */
      case 0xc8: value = (int8_t)(value-(int8_t)u8(&s)); break;
      case 0xcc: value = (int8_t)u8(&s); break;
      case 0xc9: value &= u8(&s); break;
      case 0xfa: case 0xf9: case 0xf5: {
        unsigned p = u16(&s);
        if ((cmd == 0xfa && value == 0) || (cmd == 0xf9 && value < 0) || (cmd == 0xf5 && value >= 0)) s.pc = p;
        break;
      }
      default: return 0;
    }
  }
  return 0;
}
typedef struct Envelope { Bytes data; unsigned base, index, left, steps; double value, delta; int hang, bad; } Envelope;
static double envelope_tick(Envelope *e) {
  while (!e->left && !e->hang && !e->bad) {
    unsigned p = e->base+e->index*4;
    if (!span(e->data, p, 4) || ++e->steps > 4096) { e->bad=1; break; }
    int d = sf64_bes16(e->data.p+p), v = sf64_bes16(e->data.p+p+2);
    e->index++;
    if (d == -1) e->hang=1;
    else if (d == -2) { if (v < 0) e->bad=1; else e->index=v; }
    else if (d == -3) e->index=0;
    else if (d == 0) { e->value=0; e->hang=1; }
    else if (d < 0) e->bad=1;
    else { e->left=d >= 4 ? d*3/4 : d; double t=v/32767.0; e->delta=(t*t-e->value)/e->left; }
  }
  if (e->left) { e->left--; e->value+=e->delta; }
  return e->value;
}
static double pitch(unsigned note) { return pow(2.0, ((int)note-39)/12.0); }
static double sample_at(const Sample *s, double pos) {
  uint32_t i=(uint32_t)pos; double f=pos-i;
  int16_t a, z;
  if (i >= s->end) {
    if (!s->count) return 0;
    i=s->start+(i-s->start)%(s->end-s->start);
    a=s->loop_pcm[i-(s->start/16)*16];
    unsigned next=i+1 == s->end ? s->start : i+1;
    z=s->loop_pcm[next-(s->start/16)*16];
  } else {
    a=s->pcm[i];
    z=i+1 < s->end ? s->pcm[i+1] : s->count ? s->loop_pcm[s->start%16] : 0;
  }
  return a+(z-a)*f;
}
static int render_score(Bank *b, Score *score, int16_t **out, unsigned *frames,
                         unsigned *loop_start, unsigned *loop_end) {
  float *mix=calloc((size_t)MAX_FRAMES*2, sizeof(float)); if (!mix) return 0;
  unsigned end=0; int ok=1;
  *loop_start=*loop_end=0;
  for (unsigned layer=0; layer<3 && ok; layer++) {
    double pos=0, prev_end=-1; unsigned prev_sample=0, prev_env=0, env_ticks=0;
    int prev_seq_env=0; Envelope env={0};
    for (unsigned k=0; k<score->count && ok; k++) {
      const Event *e=&score->events[k]; if (e->layer != layer) continue;
      Sample s; unsigned select=e->mode && e->target > e->note ? e->target : e->note;
      if (!sample_load(b, e->settings.instrument, select, &s)) { ok=0; break; }
      unsigned ep=e->settings.env ? e->settings.env : s.envelope;
      if (!e->continuous || prev_sample != s.address || prev_end != e->tick ||
          prev_env != ep || prev_seq_env != e->settings.seq_env) {
        pos=0; env_ticks=0;
        env=(Envelope){.data=e->settings.seq_env ? b->seq : b->font, .base=ep};
      }
      prev_sample=s.address; prev_env=ep; prev_seq_env=e->settings.seq_env;
      unsigned start=(unsigned)(e->tick*SF64_AUDIO_RATE/96.0);
      double duration=e->delay/96.0, hold=duration*(1-e->gate/256.0);
      unsigned decay=e->settings.decay ? e->settings.decay : s.decay;
      double release=decay ? 1.0/(decay*(3.0/2560.0)/3*180) : 0.02;
      if (release>1) release=1;
      int continuing=0;
      for (unsigned j=k+1; j<score->count; j++) if (score->events[j].layer == layer) {
        continuing=e->continuous && score->events[j].continuous && score->events[j].tick == e->tick+e->delay && !e->gate;
        break;
      }
      unsigned n=(unsigned)((hold+(continuing ? 0 : release))*SF64_AUDIO_RATE);
      double rate=s.tuning*pitch(e->note);
      if (score->engine) {
        if (!s.count || e->mode) { sample_free(&s); ok=0; break; }
        *loop_start=(unsigned)ceil(s.start/rate); *loop_end=(unsigned)ceil(s.end/rate);
        n=*loop_end; hold=n/(double)SF64_AUDIO_RATE+1;
      }
      if (start >= MAX_FRAMES) { sample_free(&s); continue; }
      if (n>MAX_FRAMES-start) n=MAX_FRAMES-start;
      double gain=(e->velocity*e->velocity/16129.0)*e->settings.volume;
      double pan=e->settings.pan/127.0; if (pan>1) pan=1;
      double left=sqrt(1-pan), right=sqrt(pan), released=-1;
      unsigned tick_origin=env_ticks;
      for (unsigned i=0; i<n; i++) {
        double t=i/(double)SF64_AUDIO_RATE;
        while (env_ticks <= tick_origin+(unsigned)(t*180)) { envelope_tick(&env); env_ticks++; }
        if (env.bad) { ok=0; break; }
        double level=env.value;
        if (t>=hold) { if (released<0) released=level; level=fmax(0,released-(t-hold)/release); }
        double step=rate;
        if (e->mode) {
          unsigned mode=e->mode&127;
          double from=pitch(e->note)*s.tuning, to=pitch(e->target)*s.tuning;
          if (mode==1 || mode==3 || mode==5) { double x=from; from=to; to=x; }
          double length=e->mode&128 ? duration*e->porta_time/256.0 : e->porta_time/180.0;
          double u=fmin(1,(floor(t*180)+1)/180.0/length);
          /* gBendPitchOneOctaveFrequencies[128..255] spans 1..2. */
          step=from+(to-from)*(pow(2.0,floor(u*127)/127.0)-1);
        }
        double v=sample_at(&s,pos)*gain*level;
        mix[(start+i)*2]+=(float)(v*left); mix[(start+i)*2+1]+=(float)(v*right);
        pos+=step;
      }
      prev_end=e->tick+e->delay;
      if (start+n>end) end=start+n;
      sample_free(&s);
    }
  }
  if (!ok || !end) { free(mix); return 0; }
  int16_t *pcm=malloc(end*2*sizeof(int16_t)); if (!pcm) { free(mix); return 0; }
  /* Fixed headroom retains relative cue levels. Only over-range combinations
   * receive further attenuation; no per-cue loudness normalisation. */
  float peak=0;
  for (unsigned i=0;i<end*2;i++) if (fabsf(mix[i])>peak) peak=fabsf(mix[i]);
  double gain=peak>60000 ? 30000.0/peak : 0.5;
  for (unsigned i=0;i<end;i++) {
    double fade=!score->engine && end-i<320 ? (end-i)/320.0 : 1;
    pcm[i*2]=clamp16(lrint(mix[i*2]*gain*fade)); pcm[i*2+1]=clamp16(lrint(mix[i*2+1]*gain*fade));
  }
  free(mix); *out=pcm; *frames=end; return peak>0;
}
static void le16(uint8_t *p, unsigned v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void le32(uint8_t *p, unsigned v) { le16(p,v); le16(p+2,v>>16); }
static int write_wav(const char *path, const int16_t *pcm, unsigned frames,
                      unsigned ls, unsigned le, char hash[65]) {
  size_t size=44+frames*4+(le ? 68 : 0); uint8_t *d=calloc(size,1); if (!d) return 0;
  memcpy(d,"RIFF",4); le32(d+4,(unsigned)size-8); memcpy(d+8,"WAVEfmt ",8);
  le32(d+16,16); le16(d+20,1); le16(d+22,2); le32(d+24,SF64_AUDIO_RATE);
  le32(d+28,SF64_AUDIO_RATE*4); le16(d+32,4); le16(d+34,16);
  memcpy(d+36,"data",4); le32(d+40,frames*4);
  for (unsigned i=0;i<frames*2;i++) le16(d+44+i*2,(uint16_t)pcm[i]);
  if (le) {
    uint8_t *p=d+44+frames*4; memcpy(p,"smpl",4); le32(p+4,60);
    le32(p+16,1000000000u/SF64_AUDIO_RATE); le32(p+20,60); le32(p+36,1);
    le32(p+52,ls); le32(p+56,le-1);
  }
  uint8_t digest[32]; sha256_compute(d,size,digest);
  for (int i=0;i<32;i++) sprintf(hash+i*2,"%02x",digest[i]);
  FILE *f=fopen(path,"wb"); int ok=0;
  if (f) { ok=fwrite(d,1,size,f)==size; if (fclose(f)) ok=0; }
  free(d); return ok;
}
static int file_hash(const char *path, char hex[65]) {
  FILE *f=fopen(path,"rb"); if (!f) return 0;
  if (fseek(f,0,SEEK_END)) { fclose(f); return 0; }
  long n=ftell(f); rewind(f);
  if (n<44 || n>MAX_FRAMES*4+112) { fclose(f); return 0; }
  uint8_t *d=malloc(n); int ok=d && fread(d,1,n,f)==(size_t)n; fclose(f);
  if (ok) { uint8_t h[32]; sha256_compute(d,n,h); for (int i=0;i<32;i++) sprintf(hex+i*2,"%02x",h[i]); }
  free(d); return ok;
}
int sf64_audio_cache_valid(const char *dir) {
  char path[1024], line[160]; snprintf(path,sizeof(path),"%s/audio/manifest.sha256",dir);
  FILE *f=fopen(path,"rb"); if (!f) return 0;
  int ok=fgets(line,sizeof(line),f) && !strcmp(line,"arwing64-audio-v1 " SF64_US11_SHA1 "\n");
  for (unsigned i=0;i<SF64_AUDIO_CUES && ok;i++) {
    char expected[65], name[80], actual[65];
    ok=fgets(line,sizeof(line),f) && sscanf(line,"%64s %79s",expected,name)==2 && strlen(expected)==64 && !strcmp(name,kCues[i].file);
    snprintf(path,sizeof(path),"%s/audio/%s",dir,kCues[i].file);
    if (ok) ok=file_hash(path,actual) && !strcmp(actual,expected);
  }
  if (ok) ok=fgetc(f)==EOF;
  fclose(f); return ok;
}
int sf64_audio_extract(const Sf64Rom *rom, const char *dir, const char **error) {
  Bank b; if (!bank_open(rom,&b)) return fail(error,"invalid SF64 audio tables");
  char path[1024], hashes[SF64_AUDIO_CUES][65];
  if (!dir || strlen(dir)>850) return fail(error,"audio cache path too long");
  snprintf(path,sizeof(path),"%s/audio",dir); make_dir(path);
  for (unsigned i=0;i<SF64_AUDIO_CUES;i++) {
    Score score={.engine=i==12};
    if (!score_parse(&b,kCues[i].id,&score)) return fail(error,"unsupported SF64 player audio script");
    int16_t *pcm=NULL; unsigned frames=0,ls=0,le=0;
    if (!render_score(&b,&score,&pcm,&frames,&ls,&le)) { free(pcm); return fail(error,"invalid SF64 instrument or envelope"); }
    snprintf(path,sizeof(path),"%s/audio/%s",dir,kCues[i].file);
    int ok=write_wav(path,pcm,frames,ls,le,hashes[i]); free(pcm);
    if (!ok) return fail(error,"cannot write SF64 audio cache");
  }
  snprintf(path,sizeof(path),"%s/audio/manifest.sha256",dir);
  FILE *f=fopen(path,"wb"); if (!f) return fail(error,"cannot write audio manifest");
  int ok=fprintf(f,"arwing64-audio-v1 " SF64_US11_SHA1 "\n")>0;
  for (unsigned i=0;i<SF64_AUDIO_CUES;i++) if (fprintf(f,"%s  %s\n",hashes[i],kCues[i].file)<0) ok=0;
  if (fclose(f)) ok=0;
  return ok && sf64_audio_cache_valid(dir) ? 1 : fail(error,"audio cache verification failed");
}
