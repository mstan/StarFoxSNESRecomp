#pragma once

#include "starfox/simulation/math.hpp"
#include <algorithm>
#include <array>
#include <cstdint>

// MSHOWGRID projection, following Star Fox Enhanced's DustRenderer::draw_grid.
// The caller supplies the retail vanishing point and writes only host pixels.
// A callback avoids allocating and scanning another full frame for 225 points.
template <class Plot>
void StarFoxGroundDots(const std::array<std::int16_t, 3>& camera,
                      const starfox::simulation::MatrixQ15& matrix,
                      int origin_x, int origin_y, int width, int height,
                      Plot plot) {
  using namespace starfox::simulation;
  const auto start = [](std::int16_t value) {
    return wrap16(static_cast<int>((static_cast<std::uint16_t>(value) & 255u)
                                   ^ 255u) - 1920);
  };
  auto row = transform_q15(matrix, {start(camera[0]),
      wrap16(-static_cast<std::int32_t>(camera[1])), start(camera[2])});
  std::array<std::int16_t, 3> dx{}, dz{};
  for (unsigned axis = 0; axis < 3; ++axis) {
    dx[axis] = wrap16(arithmetic_shift_right(matrix[axis], 7));
    dz[axis] = wrap16(arithmetic_shift_right(matrix[axis + 6], 7));
  }
  const auto clipped_plot = [&](int x, int y) {
    if (x >= 0 && y >= 0 && x < width && y < height) plot(x, y);
  };
  for (unsigned z = 0; z < 15; ++z) {
    auto point = row;
    for (unsigned x = 0; x < 15; ++x) {
      if (point[2] > 256) {
        const int depth = std::min<int>(point[2], 12287) & ~1;
        const auto reciprocal = static_cast<std::int16_t>((32767 * 256) / depth);
        const int sx = origin_x + multiply_q15(point[0], reciprocal);
        const int sy = origin_y + multiply_q15(point[1], reciprocal);
        if (sx >= 0 && sy >= 0 && sx < width && sy < height) {
          plot(sx, sy);
          if (point[2] < 512) clipped_plot(sx - 1, sy + 1);
        }
      }
      for (unsigned axis = 0; axis < 3; ++axis)
        point[axis] = add16(point[axis], dx[axis]);
    }
    for (unsigned axis = 0; axis < 3; ++axis)
      row[axis] = add16(row[axis], dz[axis]);
  }
}
