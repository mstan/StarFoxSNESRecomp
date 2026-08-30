#pragma once
#include "types.h"
#include "desktop/sdl_compat.h"

enum {
  kKeys_Null,
  kKeys_Controls,
  kKeys_Controls_Last = kKeys_Controls + 11,

  kKeys_ControlsP2,
  kKeys_ControlsP2_Last = kKeys_ControlsP2 + 11,

  kKeys_Load,
  kKeys_Load_Last = kKeys_Load + 19,
  kKeys_Save,
  kKeys_Save_Last = kKeys_Save + 19,
  kKeys_Fullscreen,
  kKeys_Reset,
  kKeys_Pause,
  kKeys_PauseDimmed,
  kKeys_Turbo,
  kKeys_WindowBigger,
  kKeys_WindowSmaller,
  kKeys_DisplayPerf,
  kKeys_ToggleRenderer,
  kKeys_PresentationDebug,
  kKeys_PresentationStepForward,
  kKeys_PresentationStepBack,
  kKeys_VolumeUp,
  kKeys_VolumeDown,
  kKeys_Total,
};

enum {
  kOutputMethod_SDL,
  kOutputMethod_SDLSoftware,
  kOutputMethod_OpenGL,
};

enum {
  kCrosshairColor_Original,
  kCrosshairColor_White,
  kCrosshairColor_Green,
  kCrosshairColor_Blue,
  kCrosshairColor_Red,
  kCrosshairColor_Yellow,
  kCrosshairColor_Cyan,
  kCrosshairColor_Magenta,
  kCrosshairColor_Orange,
  kCrosshairColor_Count,
};

typedef struct Config {
  int window_width;
  int window_height;
  bool new_renderer;
  bool enhanced_renderer;
  bool ignore_aspect_ratio;
  uint8 fullscreen;
  uint8 window_scale;
  bool enable_audio;
  bool linear_filtering;
  uint8 output_method;
  uint16 audio_freq;
  uint8 audio_channels;
  uint16 audio_samples;
  bool autosave;
  bool no_sprite_limits;
  /* Extra logical SNES pixels rendered on each side. 0 is the authentic
   * 256-pixel viewport; 71 produces 398x224, the nearest even-centered 16:9
   * viewport. Wider Enhanced-style modes use larger host-side margins. */
  uint16 widescreen_extra;
  bool widescreen_hud;
  uint8 widescreen_hud_oam_first_slot;
  uint8 widescreen_hud_oam_slots;
  uint8 widescreen_hud_oam_height;
  uint8 widescreen_hud_left_end;
  uint8 widescreen_hud_right_start;
  uint8 widescreen_hud_bg_y0;
  uint8 widescreen_hud_bg_y1;
  uint8 crosshair_color;
  bool god_mode;
  bool god_nuke;
  uint16 presentation_fps;
  bool show_fps;
  bool display_perf_title;
  bool disable_frame_delay;
  /* Persisted by the shared dashboard. --launcher overrides it so users can
   * always get back to settings after choosing direct boot. */
  bool skip_launcher;

  /* Oracle-build only. When false, main.c skips snes_oracle_init_default
   * and calls snes_oracle_set_disabled_by_game so the dispatcher refuses
   * every emu_* command with a structured warning naming the reason. For
   * Star Fox keeps this OFF in config.ini because the current repro path
   * is save-state load, which the from-boot oracle cannot follow — a
   * prior session wasted real time chasing false divergences before
   * noticing. See snes_oracle_backend.h header doc. */
  bool enable_snes9x_oracle;

  char *memory_buffer;
  const char *shader;

  bool enable_gamepad[2];
  int gamepad_deadzone;

  // Which players have keyboard controls
  uint8 has_keyboard_controls;
} Config;

enum {
  kGamepadBtn_Invalid = -1,
  kGamepadBtn_A,
  kGamepadBtn_B,
  kGamepadBtn_X,
  kGamepadBtn_Y,
  kGamepadBtn_Back,
  kGamepadBtn_Guide,
  kGamepadBtn_Start,
  kGamepadBtn_L3,
  kGamepadBtn_R3,
  kGamepadBtn_L1,
  kGamepadBtn_R1,
  kGamepadBtn_DpadUp,
  kGamepadBtn_DpadDown,
  kGamepadBtn_DpadLeft,
  kGamepadBtn_DpadRight,
  kGamepadBtn_L2,
  kGamepadBtn_R2,
  kGamepadBtn_Count,
};

extern Config g_config;

void ParseConfigFile(const char *filename);
void ConfigReloadKeyMap(const char *filename);
void WriteConfigFile(const char *filename);
int FindCmdForSdlKey(SDL_Keycode code, SDL_Keymod mod);
int FindCmdForGamepadButton(int button, uint32 modifiers);
