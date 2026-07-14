#include "common_cpu_infra.h"
#include "starfox_rtl.h"

const RtlGameInfo kStarFoxGameInfo = {
  .title = "starfox",
  .initialize = NULL,
  .run_frame = &StarFoxRunFrame,
  .draw_ppu_frame = &StarFoxDrawPpuFrame,
  .save_name_prefix = "save",
};
