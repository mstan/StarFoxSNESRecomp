/* Regression cases for visible-frame brightness and adaptive ownership. */
#include "starfox_presentation.h"
#include "snes/ppu.h"
#include <string.h>
#include <stdio.h>
#include <assert.h>

uint8_t g_ram[0x20000];
Ppu *g_ppu;
Snes *g_snes;
int snes_frame_counter;
const uint8_t *PpuGetMode2LayerCapture(const Ppu *ppu) {
  return ppu->wsMode2CaptureLayer ? &ppu->wsMode2Capture[0][0] : NULL;
}

static void scanout(Ppu *ppu, int mode, int brightness) {
  for (int line = 0; line <= 224; line++) {
    ppu->bgmode = (uint8_t)mode;
    ppu->hScroll[1] = (uint16_t)(line * 3);
    ppu->inidisp = line >= 17 && line < 206 ? (uint8_t)brightness : 0x80;
    Ppu before = *ppu;
    StarFoxPresentationCaptureLine(line);
    assert(memcmp(&before, ppu, sizeof(before)) == 0);
  }
}

int main(void) {
  static Ppu ppu;
  static uint8_t stock[256 * 224 * 4], output[520 * 224 * 4];
  g_ppu = &ppu;
  ppu.renderBuffer = stock;
  ppu.renderPitch = 256 * 4;
  ppu.screenEnabled[0] = 0x13;
  ppu.bgXsc[0] = 0x2c;
  snes_frame_counter = 100;

  // End-of-frame forced blank must not dim a visible gameplay frame.
  scanout(&ppu, 2, 15);
  assert(ppu.inidisp == 0x80);
  assert(StarFoxPresentationPpu()->inidisp == 15);
  assert(StarFoxPresentationBg2ScrollX(100) == 303);
  assert(StarFoxPresentationIsWideWorld(true, false));
  memset(output, 150, sizeof(output));
  RtlEnhancedRendererFrame frame = {0};
  frame.pixels = output; frame.pitch = 520 * 4;
  frame.width = 520; frame.height = 224; frame.widescreen_extra = 132;
  StarFoxPresentationApplyBrightness(&frame, 0);
  assert(output[0] == 150 && output[223 * frame.pitch] == 150);
  assert(output[205 * frame.pitch] == 150); // no bottom blanking stripe

  // Every native layer, including full-color host meshes, follows the fade.
  scanout(&ppu, 2, 6);
  memset(output, 150, sizeof(output));
  StarFoxPresentationApplyBrightness(&frame, 0);
  assert(output[112 * frame.pitch] == 60);
  scanout(&ppu, 2, 0);
  assert(!StarFoxPresentationIsWideWorld(true, false));
  assert(!StarFoxPresentationIsWideWorld(true, false));

  // Menus retain the stock picture; only a new host overlay needs fading.
  ppu.screenEnabled[0] = 0x07;
  scanout(&ppu, 1, 6);
  assert(!StarFoxPresentationIsWideWorld(true, false));
  memset(stock, 60, sizeof(stock)); memset(output, 0, sizeof(output));
  StarFoxPresentationRememberStock();
  for (int y = 0; y < 224; y++)
    memcpy(output + y * frame.pitch + 132 * 4, stock + y * 256 * 4, 256 * 4);
  output[112 * frame.pitch + 132 * 4] = 150;
  StarFoxPresentationApplyBrightness(&frame, 1);
  assert(output[112 * frame.pitch + 132 * 4] == 60);
  assert(output[112 * frame.pitch + 133 * 4] == 60);
  assert(output[112 * frame.pitch] == 0);

  // Authentic mode can draw the mesh directly into the stock render buffer.
  // Remember its pre-overlay pixels so aliasing cannot suppress the fade.
  frame.pixels = stock; frame.width = 256; frame.pitch = 256 * 4;
  frame.widescreen_extra = 0;
  stock[112 * frame.pitch] = 150;
  StarFoxPresentationApplyBrightness(&frame, 1);
  assert(stock[112 * frame.pitch] == 60);
  assert(stock[112 * frame.pitch + 4] == 60);

  ppu.screenEnabled[0] = 0x13;
  scanout(&ppu, 1, 15);
  assert(StarFoxPresentationIsWideWorld(true, false)); // hangar / flash
  scanout(&ppu, 2, 15);
  assert(!StarFoxPresentationIsWideWorld(true, true));
  assert(!StarFoxPresentationIsWideWorld(false, false));
  puts("PASS: current fade, blanking, centered menus, sparse world and stale scene");
  return 0;
}
