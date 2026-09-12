#include "starfox_state_menu.h"
#include "starfox_rtl.h"
#include "desktop/sdl_compat.h"
#include "snes_overlay_draw.h"
#include "snes_rewind.h"
#include "snes_savestate_menu.h"

static uint32_t s_inputs, s_prev, s_guard, s_repeat_at;
static unsigned s_generation;
static int s_repeat_dir;
static bool s_consumed;

bool StarFoxStateMenuIsOpen(void) {
  return snes_savestate_menu_is_open() || snes_rewind_is_open();
}

void StarFoxStateMenuInit(void) {
  s_generation = StarFoxStateGeneration();
  snes_rewind_configure();
}

void StarFoxStateMenuOpenSave(void) {
  if (StarFoxStateMenuIsOpen()) return;
  snes_savestate_menu_poll_open(0);
  snes_savestate_menu_poll_open(SNES_PAD_SELECT | SNES_PAD_R);
  s_consumed = true;
}

void StarFoxStateMenuOpenRewind(void) {
  if (StarFoxStateMenuIsOpen()) return;
  if (snes_rewind_open()) {
    s_prev = s_inputs;
    s_repeat_dir = 0;
    s_consumed = true;
  }
}

void StarFoxStateMenuKey(int key, int repeat) {
  if (!StarFoxStateMenuIsOpen()) return;
  s_consumed = true;
  s_guard |= s_inputs;
  if (snes_savestate_menu_is_open())
    snes_savestate_menu_handle_key(key, repeat);
  else if (!repeat && (key == (int)SDLK_ESCAPE || key == (int)SDLK_BACKSPACE))
    snes_rewind_close();
}

bool StarFoxStateMenuPoll(uint32_t inputs, uint32_t ticks) {
  const uint32_t gesture = SNES_PAD_SELECT | SNES_PAD_L;
  const bool was_open = StarFoxStateMenuIsOpen();
  bool consumed = was_open || s_consumed;
  s_consumed = false;
  s_inputs = inputs;
  s_guard &= inputs;
  /* A file load starts a new timeline. Rewind commits update the generation
   * below, after the shared ring has discarded only its newer snapshots. */
  if (s_generation != StarFoxStateGeneration()) {
    snes_rewind_shutdown();
    snes_rewind_configure();
    s_generation = StarFoxStateGeneration();
  }
  if (snes_savestate_menu_is_open()) {
    snes_savestate_menu_poll_nav(inputs, ticks);
  } else if (snes_rewind_is_open()) {
    uint32_t pressed = inputs & ~s_prev;
    if ((pressed & SNES_PAD_B) ||
        ((pressed & gesture) && (inputs & gesture) == gesture)) {
      snes_rewind_close();
    } else if (pressed & SNES_PAD_A) {
      snes_rewind_commit();
      s_generation = StarFoxStateGeneration();
    } else {
      int dir = (inputs & SNES_PAD_LEFT) ? -1 :
                (inputs & SNES_PAD_RIGHT) ? 1 : 0;
      if (dir != s_repeat_dir) {
        s_repeat_dir = dir;
        s_repeat_at = ticks + SNES_OVL_REPEAT_DELAY;
        if (dir) snes_rewind_step(dir);
      } else if (dir && (uint32_t)(ticks - s_repeat_at) < 0x80000000u) {
        snes_rewind_step(dir);
        s_repeat_at = ticks + SNES_OVL_REPEAT_RATE;
      }
    }
  } else if (!consumed) {
    uint32_t clean = inputs & ~s_guard;
    if (snes_savestate_menu_poll_open(clean)) {
      consumed = true;
    } else if ((clean & gesture) == gesture && (s_prev & gesture) != gesture) {
      StarFoxStateMenuOpenRewind();
      consumed = true; /* Never send the menu gesture to the game. */
    }
  }
  consumed |= StarFoxStateMenuIsOpen();
  if (consumed) s_guard |= inputs;
  s_prev = inputs;
  return consumed;
}

uint32_t StarFoxStateMenuGuestInput(uint32_t inputs) {
  s_guard &= inputs;
  return inputs & ~s_guard;
}

void StarFoxStateMenuNoteFrame(const uint32_t *pixels, int w, int h) {
  snes_savestate_menu_note_frame(pixels, w, h);
  snes_rewind_note_framebuffer(pixels, w, h);
  snes_rewind_note_frame();
}

void StarFoxStateMenuDraw(uint8_t *pixels, int pitch, int w, int h) {
  const uint32_t *panel;
  int pw, ph;
  if (snes_savestate_menu_overlay_image(&panel, &pw, &ph))
    snes_ovl_blit_panel(pixels, pitch, w, h, panel, pw, ph);
  else if (snes_rewind_overlay_image(&panel, &pw, &ph))
    snes_ovl_blit_panel_rect(pixels, pitch, w, h, panel, pw, ph,
                             0, h * 2 / 3, w, h - h * 2 / 3);
}

void StarFoxStateMenuShutdown(void) {
  snes_savestate_menu_close();
  snes_rewind_shutdown();
}
