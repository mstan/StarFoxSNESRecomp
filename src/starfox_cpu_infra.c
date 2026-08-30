#include "common_cpu_infra.h"
#include "starfox_rtl.h"

const RtlGameInfo kStarFoxGameInfo = {
  .title = "starfox",
  .initialize = NULL,
  .run_frame = &StarFoxRunFrame,
  .draw_ppu_frame = &StarFoxDrawPpuFrame,
  .enhanced_render_frame = &StarFoxEnhancedRenderFrame,
  .save_name_prefix = "save",
};
