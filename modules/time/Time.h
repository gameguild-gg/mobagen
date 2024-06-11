//
// Created by Alexandre Tolstenko Nogueira on 2024.06.11.
//

#ifndef MOBAGEN_TIME_H
#define MOBAGEN_TIME_H

#include "TimeDefs.h"

namespace MoBaGEn::TimeManager {
  class Time {
  public:
    /**
     * @brief Get the current wall time
     *
     * @return TimePoint
     */
    static TimePoint Now();

  private:
    static inline TimePoint m_startTime = Now();

  public:
    /**
     * @brief Get the Elapsed Time since the start of the program
     *
     * @return Duration
     */
    static Duration GetElapsedTime();
  };
}  // namespace MoBaGEn::TimeManager
#endif  // MOBAGEN_TIME_H
