//
// Created by Alexandre Tolstenko Nogueira on 2024.06.12.
//

#ifndef MOBAGEN_TIMER_H
#define MOBAGEN_TIMER_H

#include "TimeDefs.h"
#include "Time.h"

namespace MoBaGEn::TimeManager {
  /**
   * @brief Timer class
   * @details A class to measure wall clock time
   */
  class Timer {
  private:
    TimePoint m_startTime = Time::Now();
    Duration m_elapsedTime = Duration(0);
    bool m_running = false;

  public:
    /**
     * @brief Construct a new Timer object, by default it starts the timer
     * @param startOnCreation Start the timer on creation
     */
    explicit Timer(bool startOnCreation = true);
    ~Timer() = default;

    // delete all implicit copy and move constructors
    Timer(const Timer&) = delete;
    Timer(Timer&&) = delete;
    Timer& operator=(const Timer&) = delete;
    Timer& operator=(Timer&&) = delete;

    /**
     * @brief Start or resume the timer
     * @details It will update the start time and mark the timer as running
     */
    void Start();
    /**
     * @brief Stop or pause the timer
     * @return Duration The elapsed time since the timer started
     * @details It will update the elapsed time and mark the timer as not running, and return
     */
    Duration Stop();

    /**
     * @brief Reset the timer and restart it
     * @details It will reset the elapsed time to 0 and start the timer
     */
    void Reset();
  };
}  // namespace MoBaGEn::TimeManager

#endif  // MOBAGEN_TIMER_H
