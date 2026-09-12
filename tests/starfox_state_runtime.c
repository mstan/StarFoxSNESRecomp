/* Exercise the actual host, title scheduler, shared menus and serializers.
 * Run from an empty directory: this test uses only its own slot 12. */
#define STARFOX_DESKTOP_ENTRY StarFoxDesktopMain
#include "../src/main.c"
#include "cpu_state.h"
#include "snes_rewind.h"
#include "snes_savestate_menu.h"
#include "snes_overlay_draw.h"
#include "common/launcher_binds.h"
#include <assert.h>

static void check(bool ok, const char *message) {
  if (!ok) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
  printf("PASS: %s\n", message);
}

static void frame(uint32_t input) {
  StarFoxEnhancedPreFrame(input);
  RtlRunFrame(input);
  StarFoxEnhancedPostFrame(input);
  RtlDrawPpuFrame(g_presentation_pixels, g_snes_width * 4, g_ppu_render_flags);
}

static void replay(int n) {
  for (int i = 0; i < n; i++) frame(i < n / 2 ? SNES_PAD_LEFT : SNES_PAD_A);
}

static void check_bindings(void) {
  const char *path = "binding-test.ini";
  FILE *f = fopen(path, "w");
  check(f != NULL, "binding test config opens");
  fputs("[KeyMap]\nLoad=F1,F2,F3,F4,F5,F6,F7,F8,F9,F10\n", f);
  fclose(f);
  ConfigReloadKeyMap(path);
  check(FindCmdForSdlKey(SDLK_F7, 0) == kKeys_SaveStateMenu &&
        FindCmdForSdlKey(SDLK_F8, 0) == kKeys_Rewind, "F7/F8 match PSX menu defaults with legacy config");
  check(FindCmdForSdlKey(SDLK_F11, 0) == kKeys_Load + 6 &&
        FindCmdForSdlKey(SDLK_F12, 0) == kKeys_Load + 7, "legacy slot loads migrate to F11/F12");
  LauncherModel model = {0};
  g_launcher_config_path = path;
  launcher_binds_set_hotkey(&model, LNG_HK_SAVE_STATE_MENU, SDLK_F9, KMOD_CTRL);
  launcher_binds_set_hotkey(&model, LNG_HK_REWIND, SDLK_F9, 0);
  ConfigReloadKeyMap(path);
  check(FindCmdForSdlKey(SDLK_F9, KMOD_CTRL) == kKeys_SaveStateMenu &&
        FindCmdForSdlKey(SDLK_F9, 0) == kKeys_Rewind, "recomp-ui rebinds reach the game including modifiers and slot conflicts");
  check(!FindCmdForSdlKey(SDLK_F7, 0) && !FindCmdForSdlKey(SDLK_F8, 0), "rebind disables former defaults");
  launcher_binds_set_hotkey(&model, LNG_HK_SAVE_STATE_MENU, 0, 0);
  launcher_binds_set_hotkey(&model, LNG_HK_REWIND, 0, 0);
  ConfigReloadKeyMap(path);
  check(!FindCmdForSdlKey(SDLK_F7, 0) && !FindCmdForSdlKey(SDLK_F8, 0) &&
        !FindCmdForSdlKey(SDLK_F9, KMOD_CTRL), "recomp-ui clear stays unbound after reload");
  launcher_ini_kv_write(path, "KeyMap", "SaveStateMenu", "None");
  launcher_ini_kv_write(path, "KeyMap", "Rewind", "(unbound)");
  ConfigReloadKeyMap(path);
  check(!FindCmdForSdlKey(SDLK_F7, 0) && !FindCmdForSdlKey(SDLK_F8, 0), "PSX unbound tokens are accepted");
  launcher_ini_kv_write(path, "KeyMap", "SaveStateMenu", "F7,Ctrl+F9");
  launcher_ini_kv_write(path, "KeyMap", "Rewind", "F8");
  ConfigReloadKeyMap(path);
  check(FindCmdForSdlKey(SDLK_F7, 0) == kKeys_SaveStateMenu &&
        FindCmdForSdlKey(SDLK_F9, KMOD_CTRL) == kKeys_SaveStateMenu, "PSX comma-separated hotkey alternatives work");
  g_launcher_config_path = NULL;
}

int main(int argc, char **argv) {
  check(argc == 2 || (argc == 3 && !strcmp(argv[2], "--resume")), "owner ROM supplied");
  if (argc == 2)
    check(fopen("saves/save11.sav", "rb") == NULL, "isolated test slot is empty");
  FILE *rom_file = fopen(argv[1], "rb");
  check(rom_file != NULL, "ROM opens");
  fseek(rom_file, 0, SEEK_END);
  long size = ftell(rom_file);
  rewind(rom_file);
  uint8_t *rom = malloc(size);
  check(fread(rom, 1, size, rom_file) == (size_t)size, "ROM reads");
  fclose(rom_file);
  SDL_SetMainReady();
  check(snesrecomp_sdl_init(0), "SDL initializes");
  check_bindings();
  g_audio_mutex = SDL_CreateMutex();
  g_config.new_renderer = true;
  g_config.enhanced_renderer = true;
  g_snes_width = 384; g_snes_height = 224;
  g_ws_active = true; g_ws_extra = 64;
  g_config.widescreen_extra = 64;
  g_ppu_render_flags = kPpuRenderFlags_NewRenderer;
  extern const RtlGameInfo kStarFoxGameInfo;
  RtlRegisterGame(&kStarFoxGameInfo);
  check(SnesInit(rom, size) != NULL, "Star Fox initializes");
  g_spc_player = StarFoxSpcPlayer_Create();
  g_spc_player->initialize(g_spc_player);
  PpuBeginDrawing(g_ppu, g_my_pixels, g_snes_width * 4, g_ppu_render_flags);
  MkDir("saves");
  StarFoxStateMenuInit();

  const size_t cap = 2u * 1024u * 1024u;
  uint8_t *start = malloc(cap), *control = malloc(cap), *restored = malloc(cap);
  if (argc == 3) {
    check(RtlLoadSnapshot("saves/save11.sav"), "file loads into a new process");
    replay(10);
    size_t n = RtlSaveSnapshotToMemory(restored, cap);
    FILE *expected = fopen("file-replay.bin", "rb");
    check(expected != NULL, "file replay reference exists");
    size_t cn = fread(control, 1, cap, expected);
    fclose(expected);
    check(n == cn && !memcmp(control, restored, n), "new-process replay matches original execution");
    return 0;
  }
  for (int phase = 0; phase < 3; phase++) {
    for (int i = 0; i < (phase == 2 ? 5000 : 600); i++) {
      frame(i == 200 || i == 350 ? SNES_PAD_START : 0);
      StarFoxStateMenuNoteFrame((uint32 *)g_presentation_pixels, g_snes_width, g_snes_height);
    }
    unsigned visible = 0;
    for (int pixel = 0; pixel < g_snes_width * g_snes_height; pixel++)
      visible += (((uint32 *)g_presentation_pixels)[pixel] & 0xffffffu) != 0;
    check(visible > 100, "game has reached a visible picture");
    uint64_t saved_clock = g_cpu.master_cycles;
    size_t n = RtlSaveSnapshotToMemory(start, cap);
    check(n > 0, "snapshot captures execution state");
    uint32_t old_version = 8, current_version;
    memcpy(&current_version, start + 4, 4);
    memcpy(start + 4, &old_version, 4);
    check(!RtlLoadSnapshotFromMemory(start, n), "old incomplete states are rejected");
    check(g_cpu.master_cycles == saved_clock, "rejected state leaves machine untouched");
    memcpy(start + 4, &current_version, 4);
    replay(30);
    size_t cn = RtlSaveSnapshotToMemory(control, cap);
    check(RtlLoadSnapshotFromMemory(start, n), "memory state restores");
    check(g_cpu.master_cycles == saved_clock, "CPU clock rewinds exactly");
    size_t loaded_n = RtlSaveSnapshotToMemory(restored, cap);
    check(loaded_n == n && !memcmp(start, restored, n), "restore reproduces the complete saved snapshot");
    replay(30);
    size_t rn = RtlSaveSnapshotToMemory(restored, cap);
    if (cn != rn || memcmp(control, restored, cn)) {
      size_t offset = 0;
      while (offset < cn && offset < rn && control[offset] == restored[offset]) offset++;
      fprintf(stderr, "phase %d first mismatch %zu / %zu (%02x vs %02x)\n",
              phase, offset, cn, control[offset], restored[offset]);
      FILE *f = fopen("control.bin", "wb"); fwrite(control, 1, cn, f); fclose(f);
      f = fopen("restored.bin", "wb"); fwrite(restored, 1, rn, f); fclose(f);
      check(false, "replay matches the uninterrupted machine");
    }
    check(true, "30-frame replay matches the uninterrupted machine byte for byte");
  }

  uint64_t before = g_cpu.master_cycles;
  HandleInput(SDLK_F7, 0, true);
  check(StarFoxStateMenuIsOpen(), "save browser opens");
  StarFoxStateMenuPoll(0, 1);
  StarFoxStateMenuKey(SDLK_EQUALS, 0); /* test-owned slot 12 */
  StarFoxStateMenuPoll(SNES_PAD_X, 2);
  check(StarFoxStateMenuIsOpen(), "saving keeps the browser open");
  check(g_cpu.master_cycles == before, "menu navigation does not execute guest frames");
  StarFoxStateMenuPoll(0, 3);
  StarFoxStateMenuPoll(SNES_PAD_B, 4);
  check(!StarFoxStateMenuIsOpen(), "B closes the browser");
  check(StarFoxStateMenuGuestInput(SNES_PAD_B) == 0, "closing button cannot leak to game");
  StarFoxStateMenuGuestInput(0);
  check(StarFoxStateMenuGuestInput(SNES_PAD_B) == SNES_PAD_B, "released button works again");
  replay(10);
  size_t file_n = RtlSaveSnapshotToMemory(control, cap);
  FILE *expected = fopen("file-replay.bin", "wb");
  check(expected && fwrite(control, 1, file_n, expected) == file_n, "write independent file replay reference");
  fclose(expected);
  StarFoxStateMenuOpenSave();
  StarFoxStateMenuPoll(0, 5);
  StarFoxStateMenuPoll(SNES_PAD_A, 6);
  check(!StarFoxStateMenuIsOpen(), "loading closes browser");
  check(g_cpu.master_cycles == before, "browser file load restores CPU clock");
  StarFoxStateMenuPoll(0, 7); /* reset old rewind timeline */
  check(!snes_rewind_open(), "file load clears prior rewind history");

  for (int i = 0; i < 24; i++) {
    frame(0);
    StarFoxStateMenuNoteFrame((uint32 *)g_presentation_pixels, g_snes_width, g_snes_height);
  }
  before = g_cpu.master_cycles;
  HandleInput(SDLK_F8, 0, true);
  StarFoxStateMenuPoll(0, 8);
  StarFoxStateMenuPoll(SNES_PAD_LEFT, 9);
  StarFoxStateMenuPoll(SNES_PAD_B, 10);
  check(g_cpu.master_cycles == before, "rewind cancel leaves machine untouched");
  StarFoxStateMenuPoll(0, 11);
  StarFoxStateMenuOpenRewind();
  StarFoxStateMenuPoll(0, 12);
  StarFoxStateMenuPoll(SNES_PAD_LEFT, 13);
  StarFoxStateMenuPoll(SNES_PAD_A, 14);
  check(g_cpu.master_cycles < before, "rewind selection restores an earlier frame");
  check(!StarFoxStateMenuIsOpen(), "rewind commit closes overlay");
  StarFoxStateMenuPoll(0, 15);
  check(snes_rewind_open(), "rewind retains earlier history after commit");
  snes_rewind_close();
  StarFoxStateMenuShutdown();
  puts("All Star Fox state/menu checks passed.");
  return 0;
}
