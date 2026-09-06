#include "arwing64_picture.h"
#include "arwing64.h"
#include "common_rtl.h"
#include "common_cpu_infra.h"
#include "mod_audio.h"
#include "snes/snes.h"
#include "snes/cart.h"
#include "snes/superfx.h"
#include "snes/ppu.h"
#include <stdio.h>
#include <string.h>

/* Retail RenderObjects ($01:AC1D) consumes a sorted list at $70:021E.
 * Nodes contain next, depth, three angles, flags, shape, shadow xyz, camera
 * y/x/z and material state. Only the private replay's shape word is changed.
 * Completed native pictures are matched byte-for-byte against BG1 VRAM, so
 * a delayed DMA or transition cannot pair a new ship with an older picture. */
enum { kPictureBytes = 0x5400, kPictures = 8,
       kReplacements = 9, /* player plus retail maximum eight laser bolts */
       kTileColumnBytes = 24 * 32, kWorldColumnBytes = 19 * 32 };
typedef struct Replacement {
  StarFoxEnhancedNativeShapePose pose;
  unsigned kind, next;
  uint8_t foreground[kPictureBytes];
} Replacement;
typedef struct Picture {
  unsigned valid;
  uint8_t original[kPictureBytes], background[kPictureBytes];
  Replacement replacements[kReplacements];
  unsigned count;
  int player_index;
} Picture;
static Picture pictures[kPictures], pending;
static uint16_t view_vram[32768];
static SuperFx foreground_source;
static uint8_t foreground_ram[65536];
static uint8_t replay_ram[65536];
static uint32_t before_overlay[800 * 240];
static unsigned next_picture, prepared, completed, declined, matched;
static unsigned frame_base;
static int selected = -1;
static uint64_t reset_generation;

static unsigned word(const uint8_t *ram, unsigned a) {
  return ram[a] | ((unsigned)ram[a + 1] << 8);
}

int arwing64_player_shot_kind(const uint8_t *ram, unsigned object, unsigned shape) {
  if (!ram) return 0;
  const unsigned player = word(ram, 0x1238);
  if (player < 0x336 || player >= 0x11fa || (player - 0x336) % 0x36 ||
      object < 0x336 || object >= 0x11fa || (object - 0x336) % 0x36 ||
      (ram[object + 8] & 1) || !(ram[object + 9] & 2) ||
      word(ram, object + 0x19) != player ||
      word(ram, object + 4) != shape) return 0;
  /* Retail elaser2 and playerbeam. The same elaser2 geometry is shared by
   * other owners; the firing-object pointer is essential. */
  return shape == 0xb369 ? 1 : shape == 0xb1fd ? 2 : 0;
}

static int shot_at_node(const uint8_t *ram, unsigned node) {
  unsigned object = word(g_ram, 0x121d), found = 0, kind = 0;
  uint8_t seen[70] = {0};
  while (object) {
    if (object < 0x336 || object >= 0x11fa || (object - 0x336) % 0x36 ||
        seen[(object - 0x336) / 0x36]++) return 0;
    int candidate = arwing64_player_shot_kind(g_ram, object, word(ram, node + 8));
    if (candidate && !memcmp(ram + node + 4, g_ram + object + 0x12, 3)) {
      int matches = 1;
      const unsigned camera_offsets[3] = {18, 16, 20};
      for (unsigned axis = 0; axis < 3; axis++) {
        int16_t camera = 0;
        for (unsigned component = 0; component < 3; component++) {
          int16_t relative = (int16_t)(word(g_ram, object + 12 + component * 2) -
                                       word(g_ram, 0xc1 + component * 2));
          int16_t m = (int16_t)word(g_ram, 0x161b + (component * 3 + axis) * 2);
          int32_t product = (int32_t)relative * m;
          int32_t term = product >= 0 ? product / 32768 : -((-product + 32767) / 32768);
          camera = (int16_t)(camera + term);
        }
        if (camera != (int16_t)word(ram, node + camera_offsets[axis])) matches = 0;
      }
      if (matches) { found++; kind = (unsigned)candidate; }
    }
    object = word(g_ram, object);
  }
  return found == 1 ? (int)kind : 0;
}

static void node_pose(const uint8_t *ram, unsigned node, StarFoxEnhancedNativeShapePose *p) {
  memset(p, 0, sizeof(*p));
  p->x = (int16_t)word(ram, node + 18);
  p->y = (int16_t)word(ram, node + 16);
  p->z = (int16_t)word(ram, node + 20);
  p->pitch = (uint16_t)ram[node + 4] << 8;
  p->yaw = (uint16_t)ram[node + 5] << 8;
  p->roll = (uint16_t)ram[node + 6] << 8;
  p->vanish_x = (int16_t)word(ram, 0x34) + 16;
  p->vanish_y = (int16_t)word(ram, 0x36) + 16;
  p->use_source_view_matrix = 1;
  for (unsigned i = 0; i < 9; i++)
    p->source_view_matrix[i] = (int16_t)word(g_ram, 0x161b + i * 2);
}

void arwing64_picture_reset(void) {
  memset(pictures, 0, sizeof(pictures));
  selected = -1;
  if (g_ppu) g_ppu->renderVram = NULL;
}

static bool prepare(void *context, const SuperFx *source, uint8_t *ram) {
  (void)context;
  pending.valid = 0;
  pending.count = 0;
  pending.player_index = -1;
  const unsigned player = word(g_ram, 0x1238);
  if (source->ram_size != 65536 || player < 0x336 || player >= 0x11fa ||
      (player - 0x336) % 0x36 || g_ram[0x14db] == 3) return false;
  const unsigned shape = word(g_ram, player + 4);
  /* These are player models, including all damage and cinematic variants.
   * An ambiguous match is rejected; other ships may share geometry. */
  if (shape < 0xd2e8 || shape > 0xd400 || (shape - 0xd2cc) % 28) return false;
  unsigned node = word(ram, 0x21e), found = 0, count = 0;
  uint8_t seen[65536 / 8] = {0};
  for (unsigned guard = 0; node && guard < 128; guard++) {
    if (node < 0x4c2 || node > 0x2be2 || (seen[node / 8] & (1u << (node % 8))))
      return false;
    seen[node / 8] |= 1u << (node % 8);
    if (word(ram, node + 8) == shape &&
        !memcmp(ram + node + 4, g_ram + player + 0x12, 3)) {
      found = node; count++;
    }
    node = word(ram, node);
  }
  if (node || count != 1) { declined++; return false; }
  frame_base = (unsigned)source->scbr << 10;
  if (frame_base + kPictureBytes > source->ram_size || source->scmr != 0x39)
    return false;
  /* Capture replacements in original painter order. Hide them only in this
   * private snapshot, then retain a suffix mask for each replacement. */
  for (node = word(ram, 0x21e); node; node = word(ram, node)) {
    int kind = node == found ? 0 : shot_at_node(ram, node);
    if (node != found && !kind) continue;
    if (pending.count == kReplacements) { declined++; return false; }
    Replacement *replacement = &pending.replacements[pending.count];
    if (node == found) pending.player_index = (int)pending.count;
    pending.count++;
    replacement->kind = (unsigned)kind;
    replacement->next = word(ram, node);
    node_pose(ram, node, &replacement->pose);
    ram[node + 8] = 0xcc; ram[node + 9] = 0xd2;
  }
  foreground_source = *source;
  foreground_source.pixel[0].bitpend = foreground_source.pixel[1].bitpend = 0;
  memcpy(replay_ram, ram, sizeof(replay_ram));
  memset(replay_ram + frame_base, 0, kPictureBytes);
  /* Shadows and these two background prepasses precede every body draw. */
  replay_ram[0x1a0] &= (uint8_t)~8u;
  replay_ram[0x19e] = replay_ram[0x19f] = 0;
  replay_ram[0x1be] = replay_ram[0x1bf] = 0;
  pending.valid = 1;
  prepared++;
  return true;
}

static void complete(void *context, const SuperFx *result) {
  (void)context;
  if (!result || !pending.valid) { declined++; return; }
  SuperFx foreground_result;
  for (unsigned i = 0; i < pending.count; i++) {
    Replacement *replacement = &pending.replacements[i];
    memcpy(foreground_ram, replay_ram, sizeof(foreground_ram));
    foreground_ram[0x21e] = (uint8_t)replacement->next;
    foreground_ram[0x21f] = (uint8_t)(replacement->next >> 8);
    if (!superfx_replay_snapshot(&foreground_source, foreground_ram, &foreground_result)) {
      declined++; return;
    }
    memcpy(replacement->foreground, foreground_ram + frame_base, kPictureBytes);
  }
  SuperFx *original = g_snes->cart->superfx;
  memcpy(pending.original, original->ram + frame_base, kPictureBytes);
  memcpy(pending.background, result->ram + frame_base, kPictureBytes);
  pictures[next_picture] = pending;
  next_picture = (next_picture + 1) % kPictures;
  completed++;
}

void arwing64_picture_pre_frame(void) {
  SuperFx *fx = g_snes && g_snes->cart ? g_snes->cart->superfx : NULL;
  if (!fx) return;
  const int active = arwing64_active();
  const uint64_t generation = snes_mod_audio_reset_generation();
  if (!active || generation != reset_generation) arwing64_picture_reset();
  reset_generation = generation;
  superfx_set_enhancement_mode(fx, active ? kSuperFxEnhancement_PresentationReplay
                                        : kSuperFxEnhancement_None);
  if (active && !superfx_set_presentation_replay(fx, 1, 0xac1d, prepare, complete, NULL))
    arwing64_picture_reset();
}

void arwing64_picture_begin_draw(void) {
  selected = -1;
  if (!g_ppu) return;
  g_ppu->renderVram = NULL;
  if (!arwing64_active() || (PPU_mode(g_ppu) != 1 && PPU_mode(g_ppu) != 2)) return;
  const unsigned start = PPU_bgTileAdr(g_ppu, 0);
  if (start + kPictureBytes / 2 > 32768) return;
  for (unsigned n = 0; n < kPictures; n++) {
    unsigned i = (next_picture + kPictures - 1 - n) % kPictures;
    Picture *p = &pictures[i];
    if (!p->valid) continue;
    /* The game updates the comms/meters in rows 152..191 after this task.
     * Match every world byte and retain the current HUD unchanged. */
    const uint8_t *vram = (const uint8_t *)(g_ppu->vram + start);
    unsigned column;
    for (column = 0; column < 28; column++) {
      unsigned offset = column * kTileColumnBytes;
      if (memcmp(vram + offset, p->original + offset, kWorldColumnBytes)) break;
    }
    if (column != 28) continue;
    memcpy(view_vram, g_ppu->vram, sizeof(view_vram));
    for (column = 0; column < 28; column++) {
      unsigned offset = column * kTileColumnBytes;
      memcpy((uint8_t *)(view_vram + start) + offset,
             p->background + offset, kWorldColumnBytes);
    }
    g_ppu->renderVram = view_vram;
    selected = (int)i;
    matched++;
    break;
  }
}

int arwing64_picture_pose(unsigned extra, StarFoxEnhancedNativeShapePose *pose) {
  if (selected < 0 || !arwing64_active()) return 0;
  const Picture *picture = &pictures[selected];
  if (picture->player_index < 0) return 0;
  *pose = picture->replacements[picture->player_index].pose;
  pose->widescreen_extra = (uint16_t)extra;
  return 1;
}

unsigned arwing64_picture_draw(uint8_t *pixels, size_t pitch, int width, int height,
                               const uint8_t *rom, size_t rom_size, unsigned extra) {
  StarFoxEnhancedNativeShapePose pose;
  if (!pixels || width <= 0 || width > 800 || height <= 0 || height > 240 ||
      pitch < (size_t)width * 4 || !arwing64_picture_pose(extra, &pose)) return 0;
  unsigned written = 0;
  for (unsigned i = 0; i < pictures[selected].count; i++) {
    const Replacement *replacement = &pictures[selected].replacements[i];
    pose = replacement->pose;
    pose.widescreen_extra = (uint16_t)extra;
    for (int y = 0; y < height; y++)
      memcpy(before_overlay + y * width, pixels + y * pitch, (size_t)width * 4);
    written += replacement->kind
        ? arwing64_draw_shot(pixels, pitch, width, height, rom, rom_size,
                            &pose, (int)replacement->kind, 0)
        : arwing64_draw_player(pixels, pitch, width, height, rom, rom_size, &pose, 0);
    const uint8_t *foreground = replacement->foreground;
    for (int y = 0; y < height; y++) {
      uint32_t *dst = (uint32_t *)(pixels + y * pitch);
      for (int x = 0; x < width; x++) {
        int gx = x - 16 - (int)extra, gy = y - 16;
        bool cover = y >= 168; /* current comms/meters, outside world matching */
        if (gx >= 0 && gx < 224 && gy >= 0 && gy < 152) {
          unsigned a = (gx / 8) * kTileColumnBytes + (gy / 8) * 32 + (gy & 7) * 2;
          unsigned bits = foreground[a] | foreground[a+1] |
                          foreground[a+16] | foreground[a+17];
          cover |= (bits & (0x80u >> (gx & 7))) != 0;
        }
        if (cover) dst[x] = before_overlay[y * width + x];
      }
    }
  }
  return written;
}

void arwing64_picture_debug(void (*send_line)(const char *)) {
  char text[256];
  snprintf(text, sizeof(text), "arwing64 picture prepared=%u completed=%u declined=%u matched=%u selected=%d replacements=%u guest_patch=0",
           prepared, completed, declined, matched, selected,
           selected < 0 ? 0 : pictures[selected].count);
  send_line(text);
}
