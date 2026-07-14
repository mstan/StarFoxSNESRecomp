#include "starfox_rtl.h"

#include <stdio.h>

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

enum {
  kSnesMasterClocksPerLine = 1364,
  kSnesLinesPerFrame = 262,
  kSnesVblankStartLine = 225,
};

static bool irq_pending(void);

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
  dma_startDma(g_dma, g_snesrecomp_last_hdmaen, true);
  for (int ch = 0; ch < 8; ch++) SimpleHdma_Init(&hdma[ch], &g_dma->channel[ch]);

  for (int line = 0; line <= 224; line++) {
    for (int ch = 0; ch < 8; ch++) SimpleHdma_DoLine(&hdma[ch]);
    ppu_runLine(g_ppu, line);
  }
}
