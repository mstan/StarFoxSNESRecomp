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
  StarFoxPresentationClock clock = {0};
  const uint64_t frequency = 1000000;
  uint64_t deadline = StarFoxPresentationNextDeadline(&clock, 1000000, frequency);
  assert(deadline == 1016666);
  assert(StarFoxPresentationSlot(deadline, frequency, 1, 2) == 1008333);
  assert(StarFoxPresentationSlot(deadline, frequency, 2, 2) == deadline);
  // A long simulation may miss the duplicate, but does not shift the primary
  // deadline or add a full interval after the work.
  deadline = StarFoxPresentationNextDeadline(&clock, 1020000, frequency);
  assert(deadline == 1033333);
  assert(StarFoxPresentationSlot(deadline, frequency, 1, 4) == 1020833);
  assert(StarFoxPresentationSlot(deadline, frequency, 3, 4) == 1029167);
  for (unsigned i = 3; i <= 3600; i++)
    deadline = StarFoxPresentationNextDeadline(&clock, deadline, frequency);
  assert(deadline == 61000000); // No millisecond rounding drift over a minute.
  deadline = StarFoxPresentationNextDeadline(&clock, 62000000, frequency);
  assert(deadline == 62016666); // Pause rebases; no burst of catch-up frames.
  deadline = StarFoxPresentationNextDeadline(&clock, 42, frequency);
  assert(deadline == 16708); // Defensive clock rewind handling.
  assert(StarFoxPresentationNextDeadline(&clock, 55, 0) == 55);
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
  assert(StarFoxPresentationIsWideWorld(true, false, true));
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
  assert(!StarFoxPresentationIsWideWorld(true, false, true));
  assert(!StarFoxPresentationIsWideWorld(true, false, true));

  // Menus retain the stock picture; only a new host overlay needs fading.
  ppu.screenEnabled[0] = 0x07;
  scanout(&ppu, 1, 6);
  assert(!StarFoxPresentationIsWideWorld(true, false, true));
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
  assert(StarFoxPresentationIsWideWorld(true, false, true)); // hangar / flash
  assert(!StarFoxPresentationIsWideWorld(true, false, false)); // Game Over UI
  scanout(&ppu, 2, 15);
  assert(!StarFoxPresentationIsWideWorld(true, true, true));
  assert(!StarFoxPresentationIsWideWorld(false, false, true));
  // Retail Stage 1 respawn: subtract white from BG1/BG2, leave OBJ lettering.
  ppu.cgadsub = 0xe7; ppu.cgwsel = 0x12; ppu.fixedColor = 0x7fff;
  ppu.screenEnabled[1] = 0;
  ppu.windowsel = 0x800000; ppu.window2left = 16; ppu.window2right = 240;
  scanout(&ppu, 2, 15);
  uint8_t world[4] = {180, 130, 90, 255}, lettering[4] = {180, 130, 90, 255};
  assert(StarFoxPresentationApplyPixelEffects(world, 128, 112, 256, 0));
  assert(world[0] == 0 && world[1] == 0 && world[2] == 0);
  assert(StarFoxPresentationApplyPixelEffects(lettering, 128, 112, 256, 4));
  assert(lettering[0] == 180 && lettering[1] == 130 && lettering[2] == 90);
  // The same fade must reach the newly visible widescreen world, too.
  memset(world, 255, sizeof(world));
  assert(StarFoxPresentationApplyPixelEffects(world, 100, 112, 800, 1));
  assert(world[0] == 0 && world[1] == 0 && world[2] == 0);
  for (int x = 0; x < 800; x += 799) {
    memset(world, 255, sizeof(world));
    assert(StarFoxPresentationApplyPixelEffects(world, x, 112, 800, 1));
    assert(world[0] == 0 && world[1] == 0 && world[2] == 0);
  }
  // BG1 window clipping is independent of OBJ lettering and colour math.
  ppu.cgadsub = 0; ppu.windowsel = 2; ppu.screenWindowed[0] = 1;
  ppu.window1left = 64; ppu.window1right = 192;
  scanout(&ppu, 2, 15);
  assert(!StarFoxPresentationApplyPixelEffects(world, 128, 112, 256, 0));
  assert(StarFoxPresentationApplyPixelEffects(world, 32, 112, 256, 0));
  assert(StarFoxPresentationApplyPixelEffects(lettering, 128, 112, 256, 4));
  puts("PASS: current fade, blanking, centered menus, sparse world and stale scene");
  return 0;
}
