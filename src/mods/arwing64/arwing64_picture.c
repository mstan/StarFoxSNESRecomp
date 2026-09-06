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
       kTileColumnBytes = 24 * 32, kWorldColumnBytes = 19 * 32 };
typedef struct Picture {
  unsigned valid;
  uint8_t original[kPictureBytes], background[kPictureBytes];
  uint8_t foreground[kPictureBytes];
  StarFoxEnhancedNativeShapePose pose;
} Picture;
static Picture pictures[kPictures], pending;
static uint16_t view_vram[32768];
static SuperFx foreground_source;
static uint8_t foreground_ram[65536];
static uint32_t before_overlay[800 * 240];
static unsigned next_picture, prepared, completed, declined, matched;
static unsigned frame_base;
static int selected = -1;
static uint64_t reset_generation;

static unsigned word(const uint8_t *ram, unsigned a) {
  return ram[a] | ((unsigned)ram[a + 1] << 8);
}

void arwing64_picture_reset(void) {
  memset(pictures, 0, sizeof(pictures));
  selected = -1;
  if (g_ppu) g_ppu->renderVram = NULL;
}

static bool prepare(void *context, const SuperFx *source, uint8_t *ram) {
  (void)context;
  pending.valid = 0;
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
  StarFoxEnhancedNativeShapePose *p = &pending.pose;
  memset(p, 0, sizeof(*p));
  p->x = (int16_t)word(ram, found + 18);
  p->y = (int16_t)word(ram, found + 16);
  p->z = (int16_t)word(ram, found + 20);
  p->pitch = (uint16_t)ram[found + 4] << 8;
  p->yaw = (uint16_t)ram[found + 5] << 8;
  p->roll = (uint16_t)ram[found + 6] << 8;
  p->vanish_x = (int16_t)word(ram, 0x34) + 16;
  p->vanish_y = (int16_t)word(ram, 0x36) + 16;
  p->use_source_view_matrix = 1;
  for (unsigned i = 0; i < 9; i++)
    p->source_view_matrix[i] = (int16_t)word(g_ram, 0x161b + i * 2);
  ram[found + 8] = 0xcc; ram[found + 9] = 0xd2;
  /* A second private pass exports only objects painted after the player.
   * Their nonzero pixels must remain in front of the larger host mesh. */
  foreground_source = *source;
  foreground_source.pixel[0].bitpend = foreground_source.pixel[1].bitpend = 0;
  memcpy(foreground_ram, ram, sizeof(foreground_ram));
  memset(foreground_ram + frame_base, 0, kPictureBytes);
  foreground_ram[0x21e] = ram[found];
  foreground_ram[0x21f] = ram[found + 1];
  /* Shadows and these two background prepasses precede every body draw. */
  foreground_ram[0x1a0] &= (uint8_t)~8u;
  foreground_ram[0x19e] = foreground_ram[0x19f] = 0;
  foreground_ram[0x1be] = foreground_ram[0x1bf] = 0;
  pending.valid = 1;
  prepared++;
  return true;
}

static void complete(void *context, const SuperFx *result) {
  (void)context;
  if (!result || !pending.valid) { declined++; return; }
  SuperFx foreground_result;
  if (!superfx_replay_snapshot(&foreground_source, foreground_ram, &foreground_result)) {
    declined++; return;
  }
  SuperFx *original = g_snes->cart->superfx;
  memcpy(pending.original, original->ram + frame_base, kPictureBytes);
  memcpy(pending.background, result->ram + frame_base, kPictureBytes);
  memcpy(pending.foreground, foreground_ram + frame_base, kPictureBytes);
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
  *pose = pictures[selected].pose;
  pose->widescreen_extra = (uint16_t)extra;
  return 1;
}

unsigned arwing64_picture_draw(uint8_t *pixels, size_t pitch, int width, int height,
                               const uint8_t *rom, size_t rom_size, unsigned extra) {
  StarFoxEnhancedNativeShapePose pose;
  if (!pixels || width <= 0 || width > 800 || height <= 0 || height > 240 ||
      pitch < (size_t)width * 4 || !arwing64_picture_pose(extra, &pose)) return 0;
  for (int y = 0; y < height; y++)
    memcpy(before_overlay + y * width, pixels + y * pitch, (size_t)width * 4);
  unsigned written = arwing64_draw_player(pixels, pitch, width, height,
                                          rom, rom_size, &pose, 0);
  const uint8_t *foreground = pictures[selected].foreground;
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
  return written;
}

void arwing64_picture_debug(void (*send_line)(const char *)) {
  char text[256];
  snprintf(text, sizeof(text), "arwing64 picture prepared=%u completed=%u declined=%u matched=%u selected=%d guest_patch=0",
           prepared, completed, declined, matched, selected);
  send_line(text);
}
