#include "starfox_rtl.h"
#include "widescreen.h"

#include <stdio.h>
#include <string.h>

#include "common_cpu_infra.h"
#include "cpu_state.h"
#include "snes/cart.h"
#include "snes/dma.h"
#include "snes/interp_bridge.h"
#include "snes/ppu.h"
#include "snes/snes.h"
#include "snes/superfx.h"

uint16 counter_global_frames;

static bool s_started;
static uint32_t s_resume_pc;
static uint64_t s_next_vblank_master;
static uint32_t s_portrait_cache[64][32];
static bool s_portrait_cache_valid;
static uint32_t s_side_background_cache[224][446];
static uint8_t s_side_background_valid[224][446];

enum {
  kSnesMasterClocksPerLine = 1364,
  kSnesLinesPerFrame = 262,
  kSnesVblankStartLine = 225,
};

static bool irq_pending(void);

static uint32_t ppu_rgb(uint16_t color, uint32_t brightness) {
  const uint32_t red5 = color & 31;
  const uint32_t green5 = (color >> 5) & 31;
  const uint32_t blue5 = (color >> 10) & 31;
  const uint32_t red = ((red5 << 3) | (red5 >> 2)) * brightness / 15;
  const uint32_t green =
      ((green5 << 3) | (green5 >> 2)) * brightness / 15;
  const uint32_t blue = ((blue5 << 3) | (blue5 >> 2)) * brightness / 15;
  return (red << 16) | (green << 8) | blue;
}

static uint32_t apply_fixed_color_math(uint32_t rgb, uint16_t fixed,
                                       uint8_t math, uint8_t cgwsel,
                                       uint8_t subscreen,
                                       uint32_t brightness) {
  if (!fixed || !(math & 0x03) || ((cgwsel & 2) && subscreen))
    return rgb;
  const int fr5 = fixed & 31;
  const int fg5 = (fixed >> 5) & 31;
  const int fb5 = (fixed >> 10) & 31;
  const int fr = ((fr5 << 3) | (fr5 >> 2)) * (int)brightness / 15;
  const int fg = ((fg5 << 3) | (fg5 >> 2)) * (int)brightness / 15;
  const int fb = ((fb5 << 3) | (fb5 >> 2)) * (int)brightness / 15;
  int r = (rgb >> 16) & 255;
  int g = (rgb >> 8) & 255;
  int b = rgb & 255;
  if (math & 0x80) {
    r = IntMax(r - fr, 0);
    g = IntMax(g - fg, 0);
    b = IntMax(b - fb, 0);
  } else {
    r = IntMin(r + fr, 255);
    g = IntMin(g + fg, 255);
    b = IntMin(b + fb, 255);
  }
  if (math & 0x40) {
    r >>= 1;
    g >>= 1;
    b >>= 1;
  }
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t dominant_native_color(const uint32_t *row) {
  uint32_t best = 0;
  unsigned best_count = 0;
  for (unsigned x = 16; x < 240; x++) {
    unsigned count = 1;
    for (unsigned x2 = x + 1; x2 < 240; x2++)
      count += row[x2] == row[x];
    if (count > best_count) {
      best = row[x];
      best_count = count;
    }
  }
  return best;
}

/* Dialog portraits occupy PPU x=65..96, y=160..223 in the US v1.2 HUD.
 * The game updates their tiles over multiple DMA slices; with the host's
 * frame-at-a-time PPU renderer an intermediate slice can otherwise appear as
 * horizontal palette stripes for one display frame. Preserve the last fully
 * formed portrait only across that unmistakable partial-upload signature. */
static void stabilize_dialog_portrait(void) {
  const unsigned left = g_ws_extra + 65;
  unsigned horizontal_changes = 0;
  unsigned vertical_changes = 0;
  for (unsigned y = 160; y < 224; y++) {
    const uint32_t *row = (const uint32_t *)(g_ppu->renderBuffer +
                                             (size_t)y * g_ppu->renderPitch);
    for (unsigned x = left + 1; x < left + 32; x++)
      horizontal_changes += row[x] != row[x - 1];
  }
  for (unsigned x = left; x < left + 32; x++) {
    const uint32_t *first_row =
        (const uint32_t *)(g_ppu->renderBuffer +
                           (size_t)160 * g_ppu->renderPitch);
    uint32_t previous = first_row[x];
    for (unsigned y = 161; y < 224; y++) {
      const uint32_t *row = (const uint32_t *)(g_ppu->renderBuffer +
                                               (size_t)y *
                                                   g_ppu->renderPitch);
      vertical_changes += row[x] != previous;
      previous = row[x];
    }
  }

  const bool partial_upload =
      horizontal_changes < 400 && vertical_changes > 800;
  if (partial_upload && s_portrait_cache_valid) {
    for (unsigned y = 0; y < 64; y++) {
      uint32_t *row = (uint32_t *)(g_ppu->renderBuffer +
                                   (size_t)(160 + y) *
                                       g_ppu->renderPitch);
      memcpy(row + left, s_portrait_cache[y],
             sizeof(s_portrait_cache[y]));
    }
  } else if (horizontal_changes >= 400) {
    for (unsigned y = 0; y < 64; y++) {
      const uint32_t *row =
          (const uint32_t *)(g_ppu->renderBuffer +
                             (size_t)(160 + y) * g_ppu->renderPitch);
      memcpy(s_portrait_cache[y], row + left,
             sizeof(s_portrait_cache[y]));
    }
    s_portrait_cache_valid = true;
  }
}

static void schedule_first_vblank(void) {
  uint32_t delta;
  if (g_snes->vPos < kSnesVblankStartLine) {
    delta = (kSnesVblankStartLine - g_snes->vPos) *
            kSnesMasterClocksPerLine - g_snes->hPos;
  } else {
    delta = (kSnesLinesPerFrame - g_snes->vPos + kSnesVblankStartLine) *
            kSnesMasterClocksPerLine - g_snes->hPos;
  }
  s_next_vblank_master = g_cpu.master_cycles + delta;
}

static uint32_t clocks_until_timer_irq(void) {
  if ((!g_snes->hIrqEnabled && !g_snes->vIrqEnabled) || g_snes->inIrq)
    return UINT32_MAX;

  const uint32_t line_clocks = kSnesMasterClocksPerLine;
  const uint32_t frame_clocks = line_clocks * kSnesLinesPerFrame;
  const uint32_t target_h = g_snes->hIrqEnabled
                                ? (uint32_t)g_snes->hTimer * 4u
                                : 0u;
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
  const uint32_t current = (uint32_t)g_snes->vPos * line_clocks +
                           g_snes->hPos;
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
  uint16_t va = nmi ? (emu ? 0xfffa : 0xffea)
                    : (emu ? 0xfffe : 0xffee);
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
    if (next) s_resume_pc = next;
  }
  return true;
}

static void service_irq(void) {
  g_snes->inIrq = true;
  run_interrupt(false);
  g_snes->inIrq = false;
}

void StarFoxRunFrame(void) {
  /* Exact US v1.2 disassembly: RenderObjects is $01:AC1D; projection center X
   * and maximum X are GSU RAM $0034/$003A, with a 192-line scene. */
  /* The GSU framebuffer is 224 pixels wide and sits at PPU x=16..239. A
   * host margin of N pixels therefore needs N+16 newly projected columns on
   * each side to cover the complete output rather than leave a dead inset at
   * the new outer edge. */
  const unsigned gsu_extra = g_ws_extra ? g_ws_extra + 16 : 0;
  superfx_set_widescreen(g_snes->cart->superfx, (uint8_t)gsu_extra,
                         0x01, 0xac1d, 0x0034, 0x003a, 192);

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
  if (counter_global_frames < 16 || (counter_global_frames % 120) == 0)
    fprintf(stderr,
            "[starfox] frame=%u resume=$%06X A=%04X X=%04X Y=%04X "
            "S=%04X P=%02X E=%u DB=%02X master=%llu\n",
            counter_global_frames, (unsigned)s_resume_pc, g_cpu.A, g_cpu.X,
            g_cpu.Y, g_cpu.S, g_cpu.P, g_cpu.emulation, g_cpu.DB,
            (unsigned long long)g_cpu.master_cycles);
  counter_global_frames++;
}

void StarFoxDrawPpuFrame(void) {
  SimpleHdma hdma[8];
  uint8_t line_brightness[224] = {0};
  uint16_t line_fixed_color[224] = {0};
  uint8_t line_cgadsub[224] = {0};
  uint8_t line_cgwsel[224] = {0};
  uint8_t line_subscreen[224] = {0};
  bool wide_scene = false;
  if (g_ws_extra && g_ppu->renderBuffer) {
    const size_t width = 256u + 2u * (unsigned)g_ws_extra;
    for (unsigned y = 0; y < 224; y++)
      memset(g_ppu->renderBuffer + (size_t)y * g_ppu->renderPitch, 0,
             width * sizeof(uint32_t));
  }
  /* The mode latched between host frames is not necessarily the mode Star Fox
   * restores through HDMA for the visible picture. Always render the native
   * 256 columns centered and collect Mode 2 data opportunistically; classify
   * the scene from the modes actually observed on its scanlines below. */
  PpuSetExtraSpaceCentered(g_ppu, (uint8_t)g_ws_extra);
  PpuSetWidescreenLayerMask(g_ppu, 0);
  PpuSetMode2LayerCapture(g_ppu, g_ws_extra ? 1 : -1);
  dma_startDma(g_dma, g_snesrecomp_last_hdmaen, true);
  for (int ch = 0; ch < 8; ch++) SimpleHdma_Init(&hdma[ch], &g_dma->channel[ch]);

  for (int line = 0; line <= 224; line++) {
    for (int ch = 0; ch < 8; ch++) SimpleHdma_DoLine(&hdma[ch]);
    ppu_runLine(g_ppu, line);
    if (line > 0) {
      line_brightness[line - 1] = PPU_forcedBlank(g_ppu)
                                      ? 0
                                      : PPU_brightness(g_ppu);
      line_fixed_color[line - 1] = g_ppu->fixedColor;
      line_cgadsub[line - 1] = g_ppu->cgadsub;
      line_cgwsel[line - 1] = g_ppu->cgwsel;
      line_subscreen[line - 1] = g_ppu->screenEnabled[1];
    }
  }

  {
    /* Exact US v1.2 disassembly: MainGameInit sets RenderHUDFlag at GSU RAM
     * $021C, and RenderObjects consumes it for full gameplay scenes. It stays
     * authoritative through damage/obstacle frames that temporarily leave
     * Mode 2. Post-render Mode 2 additionally covers the no-HUD attract
     * carrier; title and controls previews are Mode 1 with this flag clear. */
    const SuperFx *fx = g_snes->cart->superfx;
    const bool render_hud =
        fx && fx->ram_size > 0x21d && (fx->ram[0x21c] | fx->ram[0x21d]);
    const unsigned mode = PPU_mode(g_ppu);
    /* LoadPreset_Title and LoadPreset_Controls both select preset $10. The
     * active 3D presets can temporarily select other modes for flashes,
     * blanking, damage, and scripted transitions even when RenderHUDFlag is
     * clear; keep those frames in the same wide presentation. The route/map
     * is entered from the title/menu preset and remains native. (US v1.2:
     * PresetSettings = WRAM $1785.) */
    const bool active_3d_preset = g_ram[0x1785] != 0x10;
    wide_scene = render_hud || mode == 2 || active_3d_preset;
  }

  if (g_ws_extra && wide_scene) {
    /* Repeat only BG2's authentic 256-column result into the new margins.
     * Keeping this copy outside the native center is a useful invariant: no
     * widescreen presentation work can alter HUD, portraits, or the real GSU
     * framebuffer. */
    const uint8_t *bg2 = PpuGetMode2LayerCapture(g_ppu);
    const unsigned width = 256u + 2u * (unsigned)g_ws_extra;
    for (unsigned y = 0; bg2 && y < 224; y++) {
      uint32_t *dst = (uint32_t *)(g_ppu->renderBuffer +
                                   (size_t)y * g_ppu->renderPitch);
      const uint32_t lower_fill =
          y >= 160 ? dominant_native_color(dst + g_ws_extra) : 0;
      for (unsigned output_x = 0; output_x < width; output_x++) {
        const int screen_x = (int)output_x - g_ws_extra;
        /* The native 224-pixel GSU view is windowed at x=16..239. Bridge its
         * otherwise-black 16-pixel insets in both the 3D scene and the lower
         * surround; preserve the authentic HUD contents at x=16..239. */
        const bool bridge_gsu_inset =
            (screen_x >= 0 && screen_x < 16) ||
            (screen_x >= 240 && screen_x < 256);
        if (screen_x >= 0 && screen_x < 256 && !bridge_gsu_inset)
          continue;
        if (y >= 160) {
          /* BG2 is deliberately hidden beneath the cockpit/HUD here. Extend
           * the dominant color actually visible on the native line instead:
           * this is the ground in gameplay and black in space, while sparse
           * HUD glyphs and hidden tile garbage cannot win the vote. */
          dst[output_x] = lower_fill;
          s_side_background_cache[y][output_x] = lower_fill;
          s_side_background_valid[y][output_x] = 1;
          continue;
        }
        /* Star Fox's usable Mode 2 landscape occupies x=16..239. Reflect from
         * those edges instead of wrapping unrelated endpoints together; the
         * old 4:3 boundary then remains position- and slope-continuous. */
        const int reflected_x = screen_x < 16 ? 31 - screen_x
                                               : 479 - screen_x;
        const int search_step = screen_x < 16 ? 1 : -1;
        uint32_t chosen = 0;
        bool found = false;
        /* BG2 can contain graphics intentionally hidden beneath the native
         * GSU framebuffer. Never reveal those in the margins: accept only a
         * BG2 sample whose RGB is actually visible in the composed native
         * frame, searching inward for the nearest unobscured background. */
        for (int attempt = 0; attempt < 224; attempt++) {
          int sx = reflected_x - 16 + search_step * attempt;
          sx %= 224;
          if (sx < 0) sx += 224;
          sx += 16;
          const unsigned sy = y;
          const uint16_t color = g_ppu->cgram[bg2[sy * 256 + sx]];
          const uint32_t candidate = ppu_rgb(color, line_brightness[sy]);
          const uint32_t compared = apply_fixed_color_math(
              candidate, line_fixed_color[sy], line_cgadsub[sy],
              line_cgwsel[sy], line_subscreen[sy], line_brightness[sy]);
          const uint32_t *native_row =
              (const uint32_t *)(g_ppu->renderBuffer +
                                 (size_t)sy * g_ppu->renderPitch);
          if (candidate == native_row[g_ws_extra + sx] ||
              compared == native_row[g_ws_extra + sx]) {
            chosen = candidate;
            found = true;
            break;
          }
        }
        if (found) {
          dst[output_x] = chosen;
          s_side_background_cache[y][output_x] = chosen;
          s_side_background_valid[y][output_x] = 1;
        } else {
          dst[output_x] = s_side_background_valid[y][output_x]
                              ? s_side_background_cache[y][output_x]
                              : 0;
        }
      }
    }
  }

  if (g_ws_extra && wide_scene) {
    const uint8_t *pixels, *valid;
    const uint8_t *bg1_palette = PpuGetMode2Bg1Palette(g_ppu);
    unsigned width, height;
    const unsigned output_width = 256u + 2u * (unsigned)g_ws_extra;

    if (superfx_get_widescreen_frame(g_snes->cart->superfx, &pixels, &valid,
                                     &width, &height)) {
      /* The native 224-pixel GSU framebuffer is centered in the 256-pixel
       * PPU viewport, leaving 16 pixels on either side. The wide capture uses
       * the same placement inside the expanded host surface. */
      /* RenderObjects also draws the fixed cockpit/HUD band near the bottom.
       * Keep that band from the authentic centered pass; only replay the 3D
       * scene above it into the side frustums. */
      for (unsigned y = 0; y < height && y < 160; y++) {
        uint32_t *dst = (uint32_t *)(g_ppu->renderBuffer +
                                     (size_t)y * g_ppu->renderPitch);
        for (unsigned raw_x = 0; raw_x < width; raw_x++) {
          /* With N+16 projected columns per side, the replay surface maps
           * directly to the host output: its authentic 224-pixel center is
           * at output x=N+16..N+239. */
          const unsigned native_left = g_ws_extra + 16;
          const unsigned native_right = native_left + 224;
          if (raw_x >= native_left && raw_x < native_right)
            continue;
          if (raw_x >= output_width || !valid[y * width + raw_x])
            continue;

          const unsigned native_width = 224;
          const unsigned replay_extra = g_ws_extra + 16;
          const unsigned capture_base =
              (native_width - replay_extra) / 2;
          const unsigned side_x = raw_x < replay_extra
                                      ? raw_x
                                      : raw_x - native_width - replay_extra;
          const unsigned palette_x = 16 + capture_base + side_x;
          const uint8_t palette_base =
              bg1_palette[y * 256 + palette_x];
          uint16_t color =
              g_ppu->cgram[palette_base + pixels[y * width + raw_x]];
          uint32_t brightness = line_brightness[y];
          uint32_t red5 = color & 31;
          uint32_t green5 = (color >> 5) & 31;
          uint32_t blue5 = (color >> 10) & 31;
          uint32_t red = ((red5 << 3) | (red5 >> 2)) * brightness / 15;
          uint32_t green = ((green5 << 3) | (green5 >> 2)) * brightness / 15;
          uint32_t blue = ((blue5 << 3) | (blue5 >> 2)) * brightness / 15;
          dst[raw_x] = (red << 16) | (green << 8) | blue;
        }
      }
    }
  }

  if (g_ws_extra && wide_scene) {
    /* Fixed-color flashes are windowed to the native 224-pixel viewport by
     * the game. Carry the same line-local color math into the new side view
     * after both PPU background and GSU polygon composition. */
    const int left0 = 0, left1 = 16 + g_ws_extra;
    const int right0 = 240 + g_ws_extra;
    const int right1 = 256 + 2 * g_ws_extra;
    for (int y = 0; y < 192; y++) {
      uint16_t fixed = line_fixed_color[y];
      uint8_t math = line_cgadsub[y];
      if (!fixed || !(math & 0x03) ||
          ((line_cgwsel[y] & 2) && line_subscreen[y]))
        continue;
      uint32_t brightness = line_brightness[y];
      int fr = (((fixed & 31) << 3) | ((fixed & 31) >> 2)) * brightness / 15;
      int fg5 = (fixed >> 5) & 31;
      int fb5 = (fixed >> 10) & 31;
      int fg = ((fg5 << 3) | (fg5 >> 2)) * brightness / 15;
      int fb = ((fb5 << 3) | (fb5 >> 2)) * brightness / 15;
      uint32_t *dst = (uint32_t *)(g_ppu->renderBuffer +
                                   (size_t)y * g_ppu->renderPitch);
      for (int span = 0; span < 2; span++) {
        int x0 = span ? right0 : left0;
        int x1 = span ? right1 : left1;
        for (int x = x0; x < x1; x++) {
          int r = (dst[x] >> 16) & 255;
          int g = (dst[x] >> 8) & 255;
          int b = dst[x] & 255;
          if (math & 0x80) {
            r = IntMax(r - fr, 0); g = IntMax(g - fg, 0);
            b = IntMax(b - fb, 0);
          } else {
            r = IntMin(r + fr, 255); g = IntMin(g + fg, 255);
            b = IntMin(b + fb, 255);
          }
          if (math & 0x40) { r >>= 1; g >>= 1; b >>= 1; }
          dst[x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
      }
    }
  }

  if (g_ws_extra && wide_scene)
    stabilize_dialog_portrait();
  else {
    s_portrait_cache_valid = false;
    memset(s_side_background_valid, 0, sizeof(s_side_background_valid));
  }

  /* The PPU center displayed the framebuffer uploaded before the newest GSU
   * completion. Promote that completion only after this picture so the side
   * replay joins the matching native center on the next display frame. */
  superfx_latch_widescreen_frame(g_snes->cart->superfx);
}
