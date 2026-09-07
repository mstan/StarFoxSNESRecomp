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
bool StarFoxPresentationIsWideWorld(bool source_current, bool controls,
                                    bool flight_scene);
/* BG1..BG4=0..3, math-enabled OBJ palettes=4, backdrop=5, other OBJ=6.
 * Returns false for a masked layer; pixels are BGRA before brightness. */
bool StarFoxPresentationApplyPixelEffects(uint8_t pixel[4], int x, int y,
                                         int width, unsigned layer);
void StarFoxPresentationApplyWorldEffects(uint8_t *pixels, size_t pitch,
                                         int width, int height);
void StarFoxPresentationApplyBrightness(const RtlEnhancedRendererFrame *frame,
                                       int stock_center);
/* Host-only cadence for opt-in presentation rates above 60. */
typedef struct StarFoxPresentationClock {
  uint64_t epoch, frame, deadline;
} StarFoxPresentationClock;
uint64_t StarFoxPresentationNextDeadline(StarFoxPresentationClock *clock,
                                        uint64_t now, uint64_t frequency);
uint64_t StarFoxPresentationSlot(uint64_t deadline, uint64_t frequency,
                                 unsigned slot, unsigned count);
#ifdef __cplusplus
}
#endif
#endif
