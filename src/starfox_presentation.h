#ifndef STARFOX_PRESENTATION_H
#define STARFOX_PRESENTATION_H
#include "common_cpu_infra.h"
struct Ppu;
#ifdef __cplusplus
extern "C" {
#endif
void StarFoxPresentationCaptureLine(int line);
const struct Ppu *StarFoxPresentationPpu(void);
const uint8_t *StarFoxPresentationPublishedBg1(void);
unsigned StarFoxPresentationBrightness(int line);
int16_t StarFoxPresentationBg2ScrollX(int line);
void StarFoxPresentationRememberStock(void);
bool StarFoxPresentationIsWideWorld(bool source_current, bool controls);
void StarFoxPresentationApplyBrightness(const RtlEnhancedRendererFrame *frame,
                                       int stock_center);
#ifdef __cplusplus
}
#endif
#endif
