#include "starfox_ground_dots.hpp"
#include "starfox/render/dust_renderer.hpp"
#include <cassert>
#include <iostream>
#include <vector>

int main() {
  using namespace starfox;
  // The ground path reads no ROM assets. Its reference constructor only needs
  // a symbol; no proprietary data is used in this differential test.
  assets::RomImage rom(std::vector<std::uint8_t>(32768));
  const auto symbols = assets::SymbolMap::parse("STAR_COLS $008000\n");
  render::DustRenderer reference(rom, symbols);
  const simulation::MatrixQ15 identity{32767,0,0,0,32767,0,0,0,32767};
  const simulation::MatrixQ15 banked{30274,12539,0,-12539,30274,0,0,0,32767};
  unsigned cases = 0, visible = 0;
  for (int width : {256, 398, 526, 800}) {
    for (int phase : {-32768, -257, -1, 0, 127, 255, 256, 32767}) {
      for (int altitude : {-800, -200, 0, 200}) {
        for (const auto& matrix : {identity, banked}) {
          timing::RenderTransform camera{};
          camera.x = phase; camera.y = altitude; camera.z = -phase;
          // Match 16-bit source wrapping even at the camera's word boundary.
          camera.z = simulation::wrap16(-phase);
          render::Framebuffer expected(width, 224), actual(width, 224);
          expected.clear(0); actual.clear(0);
          reference.draw_grid(camera, matrix, expected);
          StarFoxGroundDots({simulation::wrap16(phase), simulation::wrap16(altitude),
                              simulation::wrap16(-phase)}, matrix,
              width / 2, 112, width, 224, [&](int x, int y) {
                assert(x >= 0 && x < width && y >= 0 && y < 224);
                actual.set(x, y, 126); ++visible;
              });
          assert(actual.pixels() == expected.pixels());
          ++cases;
        }
      }
    }
  }
  assert(visible > 0);
  std::cout << cases << " ground-dot views match the reference\n";
}
