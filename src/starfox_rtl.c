#include "starfox_rtl.h"

#include <stdio.h>
#include <string.h>

#include "common_cpu_infra.h"
#include "config.h"
#include "cpu_state.h"
#include "snes/cart.h"
#include "snes/dma.h"
#include "snes/interp_bridge.h"
#include "snes/ppu.h"
#include "snes/snes.h"
#include "snes/superfx.h"
#include "starfox_enhanced_renderer.h"

uint16 counter_global_frames;

static bool s_started;
static uint32_t s_resume_pc;
static uint64_t s_next_vblank_master;

enum {
  kSnesMasterClocksPerLine = 1364,
  kSnesLinesPerFrame = 262,
  kSnesVblankStartLine = 225,
  kCrosshairObjPaletteFirst = 128 + 4 * 16,
  kCrosshairTintPaletteIndex = kCrosshairObjPaletteFirst + 15,
  kSuperFxHudColour = 0x3512,
  kRamAllst = 0x121d,
  kRamAlFreeLst = 0x121f,
  kRamInternalPlayPt = 0x162c,
  kRamPShipFlags3 = 0x1563,
  kRamSpecialDelay = 0x15b1,
  kRamSpecWepCnt = 0x1634,
  kRamPcBoxObjLw = 0x15ee,
  kRamPcBoxObjRw = 0x15f0,
  kRamPcBoxObjB = 0x15f2,
  kObjBase = 0x0336,
  kObjSize = 0x36,
  kObjCount = 0x46,
  kObjNext = 0x00,
  kObjShape = 0x04,
  kObjType = 0x09,
  kObjStratPtr = 0x16,
  kObjSFlags = 0x1d,
  kObjSFlags2 = 0x1e,
  kObjHp = 0x2a,
  kObjCollFlags = 0x2e,
  kRunnerInputA = 0x100,
  kRunnerInputR = 0x800,
  kPShipNoCollisions = 0x08,
  kShapeNull = 0x9500,
  kShapeNuke = 0x97a0,
  kStratNukeExplosion = 0x21a373,
};

static bool irq_pending(void);

static uint32_t s_last_player_inputs;
static uint16_t s_nukes_before[8];
static unsigned s_nukes_before_count;
static bool s_bomb_pressed;
static bool s_god_nuke_request;
static uint16_t s_armed_god_nukes[8];
static unsigned s_armed_god_nuke_count;
static bool s_restore_superfx_hud_colour;
static uint8_t s_saved_superfx_hud_colour;

static uint16_t rgb555(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(r | ((uint16_t)g << 5) | ((uint16_t)b << 10));
}

static bool crosshair_tint555(uint8_t color, uint8_t *r, uint8_t *g,
                              uint8_t *b) {
  switch (color) {
  case kCrosshairColor_White:
    *r = 31;
    *g = 31;
    *b = 31;
    return true;
  case kCrosshairColor_Green:
    *r = 8;
    *g = 31;
    *b = 12;
    return true;
  case kCrosshairColor_Blue:
    *r = 9;
    *g = 17;
    *b = 31;
    return true;
  case kCrosshairColor_Red:
    *r = 31;
    *g = 8;
    *b = 8;
    return true;
  case kCrosshairColor_Yellow:
    *r = 31;
    *g = 28;
    *b = 8;
    return true;
  case kCrosshairColor_Cyan:
    *r = 8;
    *g = 29;
    *b = 31;
    return true;
  case kCrosshairColor_Magenta:
    *r = 31;
    *g = 12;
    *b = 31;
    return true;
  case kCrosshairColor_Orange:
    *r = 31;
    *g = 19;
    *b = 6;
    return true;
  default:
    return false;
  }
}

static bool starfox_apply_crosshair_tint(uint16_t saved[16]) {
  uint8_t tint_r, tint_g, tint_b;
  if (!crosshair_tint555(g_config.crosshair_color, &tint_r, &tint_g, &tint_b))
    return false;

  uint16_t *palette = &g_ppu->cgram[kCrosshairObjPaletteFirst];
  memcpy(saved, palette, 16 * sizeof(saved[0]));
  for (unsigned i = 1; i < 16; i++) {
    const uint16_t source = palette[i];
    const uint8_t source_r = source & 0x1f;
    const uint8_t source_g = (source >> 5) & 0x1f;
    const uint8_t source_b = (source >> 10) & 0x1f;
    uint8_t intensity = source_r > source_g ? source_r : source_g;
    if (source_b > intensity)
      intensity = source_b;
    palette[i] = rgb555((uint8_t)(tint_r * intensity / 31),
                        (uint8_t)(tint_g * intensity / 31),
                        (uint8_t)(tint_b * intensity / 31));
  }
  palette[15] = rgb555(tint_r, tint_g, tint_b);
  return true;
}

static bool starfox_crosshair_tint_active(void) {
  uint8_t r, g, b;
  return crosshair_tint555(g_config.crosshair_color, &r, &g, &b);
}

static void starfox_apply_superfx_crosshair_tint(void) {
  SuperFx *fx = g_snes && g_snes->cart ? g_snes->cart->superfx : NULL;
  if (s_restore_superfx_hud_colour || !starfox_crosshair_tint_active() || !fx ||
      fx->ram_size <= kSuperFxHudColour)
    return;

  s_saved_superfx_hud_colour = fx->ram[kSuperFxHudColour];
  fx->ram[kSuperFxHudColour] = kCrosshairTintPaletteIndex;
  s_restore_superfx_hud_colour = true;
}

static void starfox_restore_superfx_crosshair_tint(void) {
  SuperFx *fx = g_snes && g_snes->cart ? g_snes->cart->superfx : NULL;
  if (!s_restore_superfx_hud_colour)
    return;
  if (fx && fx->ram_size > kSuperFxHudColour)
    fx->ram[kSuperFxHudColour] = s_saved_superfx_hud_colour;
  s_restore_superfx_hud_colour = false;
}

static uint16_t wram_read16(uint16_t address) {
  return (uint16_t)g_ram[address] | ((uint16_t)g_ram[address + 1] << 8);
}

static void wram_write16(uint16_t address, uint16_t value) {
  g_ram[address] = (uint8_t)value;
  g_ram[address + 1] = (uint8_t)(value >> 8);
}

static bool starfox_object_pointer_valid(uint16_t pointer) {
  const unsigned relative = (unsigned)pointer - kObjBase;
  return pointer >= kObjBase && relative < kObjSize * kObjCount &&
         (relative % kObjSize) == 0;
}

static uint16_t starfox_object_next(uint16_t pointer) {
  return wram_read16((uint16_t)(pointer + kObjNext));
}

static uint16_t starfox_object_shape(uint16_t pointer) {
  return wram_read16((uint16_t)(pointer + kObjShape));
}

static uint32_t starfox_object_strategy(uint16_t pointer) {
  return (uint32_t)g_ram[pointer + kObjStratPtr] |
         ((uint32_t)g_ram[pointer + kObjStratPtr + 1] << 8) |
         ((uint32_t)g_ram[pointer + kObjStratPtr + 2] << 16);
}

static bool starfox_pointer_in_array(uint16_t pointer, const uint16_t *items,
                                     unsigned count) {
  for (unsigned i = 0; i < count; i++) {
    if (items[i] == pointer)
      return true;
  }
  return false;
}

static bool starfox_collect_active_nukes(uint16_t *out, unsigned capacity,
                                         unsigned *count_out) {
  unsigned count = 0;
  uint16_t pointer = wram_read16(kRamAllst);
  for (unsigned visited = 0; pointer != 0 && visited < kObjCount; visited++) {
    if (!starfox_object_pointer_valid(pointer))
      return false;
    if (starfox_object_shape(pointer) == kShapeNuke && count < capacity)
      out[count++] = pointer;
    pointer = starfox_object_next(pointer);
  }
  if (pointer != 0)
    return false;
  *count_out = count;
  return true;
}

static bool starfox_active_list_has(uint16_t needle) {
  uint16_t pointer = wram_read16(kRamAllst);
  for (unsigned visited = 0; pointer != 0 && visited < kObjCount; visited++) {
    if (!starfox_object_pointer_valid(pointer))
      return false;
    if (pointer == needle)
      return true;
    pointer = starfox_object_next(pointer);
  }
  return false;
}

static bool starfox_gameplay_like_active(void) {
  const uint16_t player = wram_read16(kRamInternalPlayPt);
  return player != 0 && starfox_object_pointer_valid(player) &&
         starfox_active_list_has(player);
}

static void starfox_apply_god_mode_state(void) {
  if (!g_config.god_mode || !starfox_gameplay_like_active())
    return;

  g_ram[kRamPShipFlags3] |= kPShipNoCollisions;
  if (wram_read16(kRamSpecWepCnt) < 3)
    wram_write16(kRamSpecWepCnt, 3);
}

static bool starfox_object_is_player_part(uint16_t pointer) {
  return pointer != 0 && (pointer == wram_read16(kRamInternalPlayPt) ||
                          pointer == wram_read16(kRamPcBoxObjB) ||
                          pointer == wram_read16(kRamPcBoxObjLw) ||
                          pointer == wram_read16(kRamPcBoxObjRw));
}

static bool starfox_god_nuke_shape_damage_only(uint16_t shape) {
  static const uint16_t kDamageOnlyShapes[] = {
      0xa6ef, 0xa70b, 0xa727, 0xa743, 0xa75f, 0xa77b,
  };
  return starfox_pointer_in_array(shape, kDamageOnlyShapes,
                                  sizeof(kDamageOnlyShapes) /
                                      sizeof(kDamageOnlyShapes[0]));
}

static bool starfox_god_nuke_shape_skip(uint16_t shape) {
  return shape == 0x94ac || shape == 0x94e4;
}

static void starfox_detonate_god_nuke(void) {
  enum {
    kFriendCollision = 0x80,
    kHitFlash = 0x02,
    kCollisionDisabled = 0x01,
    kNuked = 0x10,
    kRegularNukeDamage = 10,
  };

  uint16_t pointer = wram_read16(kRamAllst);
  for (unsigned visited = 0; pointer != 0 && visited < kObjCount; visited++) {
    if (!starfox_object_pointer_valid(pointer))
      return;
    const uint16_t next = starfox_object_next(pointer);
    const uint16_t shape = starfox_object_shape(pointer);
    if (starfox_object_is_player_part(pointer) || shape == kShapeNuke ||
        starfox_object_strategy(pointer) == kStratNukeExplosion ||
        (g_ram[pointer + kObjCollFlags] & kFriendCollision) ||
        (int8_t)g_ram[pointer + kObjHp] < 0) {
      pointer = next;
      continue;
    }

    if (starfox_god_nuke_shape_skip(shape)) {
      pointer = next;
      continue;
    }
    if (starfox_god_nuke_shape_damage_only(shape)) {
      g_ram[pointer + kObjHp] =
          g_ram[pointer + kObjHp] > kRegularNukeDamage
              ? (uint8_t)(g_ram[pointer + kObjHp] - kRegularNukeDamage)
              : 0;
    } else {
      g_ram[pointer + kObjSFlags2] |= kCollisionDisabled;
      g_ram[pointer + kObjHp] = 0;
    }
    g_ram[pointer + kObjSFlags] |= kHitFlash;
    g_ram[pointer + kObjType] |= kNuked;
    pointer = next;
  }
}

static void starfox_clear_god_nuke_state(void) {
  s_nukes_before_count = 0;
  s_bomb_pressed = false;
  s_god_nuke_request = false;
  s_armed_god_nuke_count = 0;
}

void StarFoxEnhancedPreFrame(uint32 inputs) {
  const uint32_t current = inputs & 0xfff;
  const uint32_t pressed = current & ~s_last_player_inputs;
  s_last_player_inputs = current;
  starfox_apply_superfx_crosshair_tint();

  if (!g_config.god_mode) {
    starfox_clear_god_nuke_state();
    return;
  }

  starfox_apply_god_mode_state();
  s_bomb_pressed = (pressed & kRunnerInputA) != 0;
  s_god_nuke_request =
      g_config.god_nuke && s_bomb_pressed && (current & kRunnerInputR) != 0;
  s_nukes_before_count = 0;
  if (s_bomb_pressed &&
      !starfox_collect_active_nukes(
          s_nukes_before, sizeof(s_nukes_before) / sizeof(s_nukes_before[0]),
          &s_nukes_before_count)) {
    s_bomb_pressed = false;
    s_god_nuke_request = false;
  }
}

void StarFoxEnhancedPostFrame(uint32 inputs) {
  StarFoxEnhancedLatchSourceFrame();
  (void)inputs;
  starfox_restore_superfx_crosshair_tint();
  if (!g_config.god_mode) {
    starfox_clear_god_nuke_state();
    return;
  }

  starfox_apply_god_mode_state();

  uint16_t active_nukes[8];
  unsigned active_nuke_count = 0;
  if (!starfox_collect_active_nukes(
          active_nukes, sizeof(active_nukes) / sizeof(active_nukes[0]),
          &active_nuke_count)) {
    starfox_clear_god_nuke_state();
    return;
  }

  if (s_bomb_pressed) {
    for (unsigned i = 0; i < active_nuke_count; i++) {
      const uint16_t pointer = active_nukes[i];
      if (starfox_pointer_in_array(pointer, s_nukes_before,
                                   s_nukes_before_count))
        continue;
      g_ram[kRamSpecialDelay] = 4;
      if (s_god_nuke_request &&
          !starfox_pointer_in_array(pointer, s_armed_god_nukes,
                                    s_armed_god_nuke_count) &&
          s_armed_god_nuke_count <
              sizeof(s_armed_god_nukes) / sizeof(s_armed_god_nukes[0])) {
        s_armed_god_nukes[s_armed_god_nuke_count++] = pointer;
      }
    }
  }

  unsigned write = 0;
  unsigned detonations = 0;
  for (unsigned i = 0; i < s_armed_god_nuke_count; i++) {
    const uint16_t pointer = s_armed_god_nukes[i];
    if (!starfox_active_list_has(pointer)) {
      continue;
    }
    const uint16_t shape = starfox_object_shape(pointer);
    if (shape == kShapeNull ||
        starfox_object_strategy(pointer) == kStratNukeExplosion) {
      detonations++;
      continue;
    }
    s_armed_god_nukes[write++] = pointer;
  }
  s_armed_god_nuke_count = write;
  for (unsigned i = 0; i < detonations; i++)
    starfox_detonate_god_nuke();

  s_bomb_pressed = false;
  s_god_nuke_request = false;
  s_nukes_before_count = 0;
}

static void schedule_first_vblank(void) {
  uint32_t delta;
  if (g_snes->vPos < kSnesVblankStartLine) {
    delta = (kSnesVblankStartLine - g_snes->vPos) * kSnesMasterClocksPerLine -
            g_snes->hPos;
  } else {
    delta = (kSnesLinesPerFrame - g_snes->vPos + kSnesVblankStartLine) *
                kSnesMasterClocksPerLine -
            g_snes->hPos;
  }
  s_next_vblank_master = g_cpu.master_cycles + delta;
}

static uint32_t clocks_until_timer_irq(void) {
  if ((!g_snes->hIrqEnabled && !g_snes->vIrqEnabled) || g_snes->inIrq)
    return UINT32_MAX;

  const uint32_t line_clocks = kSnesMasterClocksPerLine;
  const uint32_t frame_clocks = line_clocks * kSnesLinesPerFrame;
  const uint32_t target_h =
      g_snes->hIrqEnabled ? (uint32_t)g_snes->hTimer * 4u : 0u;
  if (target_h >= line_clocks)
    return UINT32_MAX;

  if (!g_snes->vIrqEnabled) {
    uint32_t delta = target_h >= g_snes->hPos
                         ? target_h - g_snes->hPos
                         : line_clocks - g_snes->hPos + target_h;
    return delta + 1;
  }

  if (g_snes->vTimer >= kSnesLinesPerFrame)
    return UINT32_MAX;
  const uint32_t current = (uint32_t)g_snes->vPos * line_clocks + g_snes->hPos;
  uint32_t target = (uint32_t)g_snes->vTimer * line_clocks + target_h;
  if (target < current)
    target += frame_clocks;
  return target - current + 1;
}

/* Advance idle hardware toward this host frame's vblank, but stop at a CPU
 * timer IRQ so the interrupted code can run at the correct beam position. */
static bool idle_hardware_toward_vblank(void) {
  if (!s_next_vblank_master)
    schedule_first_vblank();
  while (g_cpu.master_cycles < s_next_vblank_master) {
    uint64_t remaining = s_next_vblank_master - g_cpu.master_cycles;
    uint32_t chunk = remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining;
    uint32_t irq_delta = clocks_until_timer_irq();
    if (irq_delta < chunk)
      chunk = irq_delta;
    g_cpu.master_cycles += chunk;
    snes_advance_master_cycles(g_snes, chunk);
    cart_sync_coprocessors(g_snes->cart, g_cpu.master_cycles);
    if (irq_pending() && !g_cpu._flag_I)
      return false;
  }
  do {
    s_next_vblank_master +=
        (uint64_t)kSnesMasterClocksPerLine * kSnesLinesPerFrame;
  } while (s_next_vblank_master <= g_cpu.master_cycles);
  return true;
}

static uint16_t vector16(uint16_t address) {
  return (uint16_t)cart_read(g_snes->cart, 0, address) |
         ((uint16_t)cart_read(g_snes->cart, 0, (uint16_t)(address + 1)) << 8);
}

static void run_interrupt(bool nmi) {
  const bool emu = g_cpu.emulation != 0;
  uint16_t va = nmi ? (emu ? 0xfffa : 0xffea) : (emu ? 0xfffe : 0xffee);
  uint16_t target = vector16(va);
  cpu_push_interrupt_frame(&g_cpu);
  if (!interp_bridge_run_interrupt(&g_cpu, target))
    fprintf(stderr, "[starfox] interrupt LLE bailed at $00:%04X\n", target);
}

static bool irq_pending(void) {
  SuperFx *fx = g_snes->cart->superfx;
  return g_snes->inIrq || (fx && fx->irq_pending);
}

static bool run_main_until_boundary(void) {
  interp_bridge_set_master_deadline(s_next_vblank_master);
  const bool completed =
      interp_bridge_run_until_quiescent(&g_cpu, s_resume_pc) != 0;
  interp_bridge_set_master_deadline(0);
  if (!completed) {
    fprintf(stderr, "[starfox] main LLE bailed at $%06X\n",
            (unsigned)s_resume_pc);
    return false;
  }
  {
    uint32_t next = interp_bridge_lle_resume_pc();
    if (next)
      s_resume_pc = next;
  }
  return true;
}

static void service_irq(void) {
  g_snes->inIrq = true;
  run_interrupt(false);
  g_snes->inIrq = false;
}

void StarFoxRunFrame(void) {
  SuperFx *const superfx = g_snes->cart->superfx;
  if (superfx) {
    superfx_set_enhancement_mode(superfx, kSuperFxEnhancement_None);
    superfx_set_widescreen(superfx, 0, 0, 0, 0, 0, 0);
    superfx_set_widescreen_replay_zero_words(superfx, NULL, 0);
  }

  if (!s_started) {
    cpu_state_init(&g_cpu, g_ram);
    s_resume_pc = vector16(0xfffc);
    s_started = true;
  } else {
    /* The S-CPU commonly reaches a WAI/polling quiescent point well before
     * vblank.  A host frame must not inject the next NMI immediately: the
     * beam, timers, auto-joypad unit, APU and GSU continue to run during that
     * idle interval.  In particular Star Fox relies on the GSU receiving the
     * full interval between vblanks to finish its framebuffer.  Stop at each
     * timer IRQ, however, and resume the interrupted CPU before continuing to
     * vblank; otherwise beam waits immediately after an IRQ can be skipped. */
    unsigned serviced = 0;
    for (;;) {
      if (irq_pending() && !g_cpu._flag_I) {
        if (serviced++ >= 64) {
          fprintf(stderr, "[starfox] IRQ did not deassert after 64 services\n");
          return;
        }
        service_irq();
        if (!run_main_until_boundary())
          return;
        continue;
      }
      if (idle_hardware_toward_vblank())
        break;
    }
    if (irq_pending() && !g_cpu._flag_I)
      service_irq();
    if (g_snes->nmiEnabled) {
      g_snes->inNmi = true;
      run_interrupt(true);
      g_snes->inNmi = false;
    }
  }

  if (!run_main_until_boundary())
    return;
  if (!s_next_vblank_master)
    schedule_first_vblank();
#if SNESRECOMP_TRACE
  /* Boot/heartbeat CPU-state trace: trace builds only. Release builds were
   * writing one of these to stderr every 120 frames for the whole session. */
  if (counter_global_frames < 16 || (counter_global_frames % 120) == 0)
    fprintf(stderr,
            "[starfox] frame=%u resume=$%06X A=%04X X=%04X Y=%04X "
            "S=%04X P=%02X E=%u DB=%02X master=%llu\n",
            counter_global_frames, (unsigned)s_resume_pc, g_cpu.A, g_cpu.X,
            g_cpu.Y, g_cpu.S, g_cpu.P, g_cpu.emulation, g_cpu.DB,
            (unsigned long long)g_cpu.master_cycles);
#endif
  counter_global_frames++;
}

void StarFoxDrawPpuFrame(void) {
  SimpleHdma hdma[8];
  uint16_t saved_crosshair_palette[16];
  const bool restore_crosshair_palette =
      starfox_apply_crosshair_tint(saved_crosshair_palette);
  PpuSetExtraSpace(g_ppu, 0);
  PpuSetWidescreenLayerMask(g_ppu, 0);
  PpuSetWidescreenLayerViewportInset(g_ppu, 0, 0, 0);
  PpuSetWsHudOamBand(g_ppu, 0, 0, 0);
  PpuSetWsHudOamShiftRange(g_ppu, 0, 0);
  PpuSetWsHudOamShiftRange2(g_ppu, 0, 0);
  PpuSetWidescreenLayerAnchorBand(g_ppu, 0, 0, 0, 0, 0);
  PpuSetMode2LayerCapture(g_ppu, -1);
  PpuSetWidescreenLineEnhancer(g_ppu, NULL, NULL);
  dma_startDma(g_dma, g_snesrecomp_last_hdmaen, true);
  for (int ch = 0; ch < 8; ch++)
    SimpleHdma_Init(&hdma[ch], &g_dma->channel[ch]);

  for (int line = 0; line <= 224; line++) {
    for (int ch = 0; ch < 8; ch++)
      SimpleHdma_DoLine(&hdma[ch]);
    ppu_runLine(g_ppu, line);
  }

  if (restore_crosshair_palette) {
    memcpy(&g_ppu->cgram[kCrosshairObjPaletteFirst], saved_crosshair_palette,
           sizeof(saved_crosshair_palette));
  }
}
