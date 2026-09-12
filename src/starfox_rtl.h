#ifndef STARFOX_RTL_H
#define STARFOX_RTL_H

#include "common_cpu_infra.h"
#include "common_rtl.h"

void StarFoxEnhancedPreFrame(uint32 inputs);
void StarFoxEnhancedPostFrame(uint32 inputs);
RtlEnhancedRenderResult StarFoxEnhancedRenderFrame(
    RtlEnhancedRendererFrame *frame);
void StarFoxRunFrame(void);
void StarFoxDrawPpuFrame(void);
void StarFoxSaveStateExtra(struct SaveLoadInfo *sli);
void StarFoxLoadStateExtra(struct SaveLoadInfo *sli, uint32_t version);
void StarFoxStateLoaded(uint32_t version);
unsigned StarFoxStateGeneration(void);

#endif
