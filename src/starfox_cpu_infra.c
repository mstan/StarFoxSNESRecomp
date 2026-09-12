#include "common_cpu_infra.h"
#include "starfox_rtl.h"

const RtlGameInfo kStarFoxGameInfo = {
  .title = "starfox",
  .initialize = NULL,
  .run_frame = &StarFoxRunFrame,
  .draw_ppu_frame = &StarFoxDrawPpuFrame,
  .enhanced_render_frame = &StarFoxEnhancedRenderFrame,
  .save_name_prefix = "save",
  .state_save_extra = StarFoxSaveStateExtra,
  .state_load_extra = StarFoxLoadStateExtra,
  .on_state_loaded = StarFoxStateLoaded,
  .minimum_state_version = 9,
};
