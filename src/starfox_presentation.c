#include "starfox_presentation.h"
#include "common_rtl.h"
#include "snes/ppu.h"
#include <string.h>

static Ppu g_presentation_ppu;
static uint8_t g_presentation_brightness[224];
static uint8_t g_presentation_mode[224];
static int16_t g_bg2_scroll_x[224];
static uint8_t g_stock_pixels[224][256 * 4];
static int g_presentation_ppu_valid;
typedef struct PixelEffects {
  uint32_t windowsel;
  uint16_t logic, fixed;
  uint8_t left[2], right[2], windowed, main, sub, math, color;
} PixelEffects;
static PixelEffects g_effects[224];
static int g_first_visible, g_last_visible;

void StarFoxPresentationCaptureLine(int line) {
  if (line == 0) {
    g_presentation_ppu_valid = 0;
    g_first_visible = 224;
    g_last_visible = -1;
    memset(g_presentation_brightness, 0, sizeof(g_presentation_brightness));
    memset(g_presentation_mode, 0, sizeof(g_presentation_mode));
  }
  if (!g_ppu || line < 1 || line > 224)
    return;
  g_presentation_brightness[line - 1] =
      PPU_forcedBlank(g_ppu) ? 0 : PPU_brightness(g_ppu);
  g_presentation_mode[line - 1] = PPU_mode(g_ppu);
  if (g_presentation_brightness[line - 1]) {
    if (g_first_visible == 224) g_first_visible = line - 1;
    g_last_visible = line - 1;
  }
  g_bg2_scroll_x[line - 1] = (int16_t)g_ppu->hScroll[1];
  g_effects[line - 1] = (PixelEffects){
    g_ppu->windowsel, g_ppu->wbgobjlog, g_ppu->fixedColor,
    {g_ppu->window1left, g_ppu->window2left},
    {g_ppu->window1right, g_ppu->window2right},
    g_ppu->screenWindowed[0], g_ppu->screenEnabled[0],
    g_ppu->screenEnabled[1], g_ppu->cgadsub, g_ppu->cgwsel};
  /* Sample the visible world, before the bottom-of-screen HDMA blank. The
   * copy is host-only; emulated registers and memory remain untouched. */
  if (line == 112) {
    g_presentation_ppu = *g_ppu;
    g_presentation_ppu_valid = 1;
  }
}

int16_t StarFoxPresentationBg2ScrollX(int line) {
  return line >= 0 && line < 224 ? g_bg2_scroll_x[line] : 0;
}

void StarFoxPresentationRememberStock(void) {
  if (!g_ppu || !g_ppu->renderBuffer)
    return;
  for (int y = 0; y < 224; y++)
    memcpy(g_stock_pixels[y], g_ppu->renderBuffer +
               (size_t)y * g_ppu->renderPitch, sizeof(g_stock_pixels[y]));
}

const Ppu *StarFoxPresentationPpu(void) {
  return g_presentation_ppu_valid ? &g_presentation_ppu : g_ppu;
}

const uint8_t *StarFoxPresentationPublishedBg1(void) {
  return g_ppu && (g_ppu->renderFlags & kPpuRenderFlags_NewRenderer)
             ? PpuGetMode2LayerCapture(g_ppu) : NULL;
}

bool StarFoxPresentationIsWideWorld(bool source_current, bool controls,
                                    bool flight_scene) {
  const unsigned mode = g_presentation_ppu.bgmode & 7;
  if (!g_presentation_ppu_valid || !source_current ||
      (mode != 1 && mode != 2) || (mode == 1 && !flight_scene) || controls ||
      (g_presentation_ppu.screenEnabled[0] & 0x13) != 0x13 ||
      (g_presentation_ppu.bgXsc[0] & 0xfc) != 0x2c)
    return false;
  // Retail world: double-buffered Super FX BG1 with BG2 and OBJ. The title
  // uses main=07; map/briefing use Mode 3; controls have a separate IRQ flag.
  // Mode 1 is also used for the hangar, flashes and scripted world effects.
  for (int y = 32; y < 192; y++) {
    if (g_presentation_mode[y] != mode || !g_presentation_brightness[y])
      return false;
  }
  return true;
}

static bool window_inside(const PixelEffects *e, int x, unsigned layer) {
  unsigned flags = e->windowsel >> (layer * 4);
  bool w1 = x >= e->left[0] && x <= e->right[0];
  bool w2 = x >= e->left[1] && x <= e->right[1];
  if (flags & 1) w1 = !w1;
  if (flags & 4) w2 = !w2;
  if (!(flags & 2)) return (flags & 8) && w2;
  if (!(flags & 8)) return w1;
  switch ((e->logic >> (layer * 2)) & 3) {
    case 0: return w1 || w2;
    case 1: return w1 && w2;
    case 2: return w1 != w2;
    default: return w1 == w2;
  }
}

static bool window_mode(unsigned mode, bool inside) {
  return mode == 3 || (mode == 1 && !inside) || (mode == 2 && inside);
}

bool StarFoxPresentationApplyPixelEffects(uint8_t pixel[4], int x, int y,
                                         int width, unsigned layer) {
  if (!g_presentation_ppu_valid || width <= 0 || y < 0 || y >= 224)
    return true;
  int effect_y = y;
  if (g_last_visible >= 0) {
    if (effect_y < g_first_visible) effect_y = g_first_visible;
    if (effect_y > g_last_visible) effect_y = g_last_visible;
  }
  const PixelEffects *e = &g_effects[effect_y];
  unsigned window_layer = layer == 6 ? 4 : layer;
  if (window_layer < 5 && !(e->main & (1u << window_layer))) return false;
  bool masked = window_layer < 5 && (e->windowed & (1u << window_layer));
  bool fixed_math = layer < 6 && (e->math & (1u << layer)) &&
                    !((e->color & 2) && e->sub);
  if (!masked && !(e->color >> 6) && !fixed_math) return true;
  // The expanded world replaces the 224-pixel Super FX viewport, whose
  // hardware coordinates start at 16. Do not expand its unused side borders
  // into strips of scenery exempt from a full-world fade.
  int wx = width == 256 ? x : 16 + x * 224 / width;
  if (masked && window_inside(e, wx, window_layer)) return false;
  bool color_window = window_inside(e, wx, 5);
  if (window_mode(e->color >> 6, color_window))
    pixel[0] = pixel[1] = pixel[2] = 0;
  if (!fixed_math ||
      window_mode((e->color >> 4) & 3, color_window)) return true;
  // Retail wipes use the fixed colour, including TS=0 with add-subscreen
  // selected (the subscreen backdrop resolves to fixed colour in that case).
  // Live secondary-plane blending is separate from this fixed-colour pass.
  bool half = (e->math & 0x40) && !(e->color & 2);
  for (unsigned channel = 0; channel < 3; channel++) {
    unsigned five = (e->fixed >> ((2 - channel) * 5)) & 31;
    int fixed = (int)((five << 3) | (five >> 2));
    int value = pixel[channel] + ((e->math & 0x80) ? -fixed : fixed);
    if (value < 0) value = 0;
    if (half) value /= 2;
    if (value > 255) value = 255;
    pixel[channel] = (uint8_t)value;
  }
  return true;
}

void StarFoxPresentationApplyWorldEffects(uint8_t *pixels, size_t pitch,
                                         int width, int height) {
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      uint8_t *pixel = pixels + (size_t)y * pitch + (size_t)x * 4;
      if (pixel[3] && !StarFoxPresentationApplyPixelEffects(pixel, x, y, width, 0))
        memset(pixel, 0, 4);
    }
  }
}

void StarFoxPresentationApplyBrightness(const RtlEnhancedRendererFrame *frame,
                                          int stock_center) {
  if (!g_presentation_ppu_valid || !frame || !frame->pixels)
    return;
  int first = 0, last = 223;
  while (first < last && !g_presentation_brightness[first]) first++;
  while (last > first && !g_presentation_brightness[last]) last--;
  for (int y = 0; y < frame->height && y < 224; y++) {
    unsigned level = g_presentation_brightness[y];
    // Gameplay extends the world into the original top/bottom letterbox.
    // Menus keep the stock scanline blanking exactly.
    if (!stock_center && (y < first || y > last))
      level = g_presentation_brightness[y < first ? first : last];
    if (level == 15)
      continue;
    uint8_t *row = frame->pixels + (size_t)y * frame->pitch;
    for (int x = 0; x < frame->width; x++) {
      uint8_t *pixel = row + (size_t)x * 4;
      if (stock_center && x >= frame->widescreen_extra &&
          x < frame->widescreen_extra + 256) {
        const uint8_t *original = g_stock_pixels[y] +
            (size_t)(x - frame->widescreen_extra) * 4;
        if (memcmp(pixel, original, 3) == 0)
          continue; // Stock pixels already carry the correct brightness.
      }
      for (int channel = 0; channel < 3; channel++)
        pixel[channel] = (uint8_t)((unsigned)pixel[channel] * level / 15);
    }
  }
}

unsigned StarFoxPresentationBrightness(int line) {
  return line >= 0 && line < 224 ? g_presentation_brightness[line] : 0;
}

unsigned StarFoxPresentationDuplicateDelayMs(uint64_t elapsed, uint64_t frequency,
                                             unsigned fps, unsigned duplicate) {
  if (fps <= 60 || !frequency || !duplicate) return 0;
  const uint64_t deadline = frequency * duplicate / fps;
  // Rendering and simulation already spent part of this frame's budget.
  // A late duplicate must not add another full interval and slow gameplay.
  return elapsed >= deadline ? 0 : (unsigned)((deadline - elapsed) * 1000 / frequency);
}
