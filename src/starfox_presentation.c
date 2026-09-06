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

void StarFoxPresentationCaptureLine(int line) {
  if (line == 0) {
    g_presentation_ppu_valid = 0;
    memset(g_presentation_brightness, 0, sizeof(g_presentation_brightness));
    memset(g_presentation_mode, 0, sizeof(g_presentation_mode));
  }
  if (!g_ppu || line < 1 || line > 224)
    return;
  g_presentation_brightness[line - 1] =
      PPU_forcedBlank(g_ppu) ? 0 : PPU_brightness(g_ppu);
  g_presentation_mode[line - 1] = PPU_mode(g_ppu);
  g_bg2_scroll_x[line - 1] = (int16_t)g_ppu->hScroll[1];
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

bool StarFoxPresentationIsWideWorld(bool source_current, bool controls) {
  const unsigned mode = g_presentation_ppu.bgmode & 7;
  if (!g_presentation_ppu_valid || !source_current ||
      (mode != 1 && mode != 2) || controls ||
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
