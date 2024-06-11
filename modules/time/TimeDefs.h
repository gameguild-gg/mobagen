//
// Created by Alexandre Tolstenko Nogueira on 2024.06.11.
//

#ifndef MOBAGEN_TIMEDEFS_H
#define MOBAGEN_TIMEDEFS_H

#include <chrono>

namespace MoBaGEn::TimeManager {
  /**
   * @brief TimePoint is a type alias for std::chrono::time_point<std::chrono::high_resolution_clock>
   * @description rename std::chrono::time_point<std::chrono::high_resolution_clock> to TimePoint
   */
  using TimePoint = std::chrono::time_point<std::chrono::high_resolution_clock>;

  /**
   * @brief Duration is a type alias for std::chrono::duration<double>
   * @description rename std::chrono::duration<double> to Duration
   */
  using Duration = std::chrono::duration<double>;
}  // namespace MoBaGEn::TimeManager

#endif  // MOBAGEN_TIMEDEFS_H
