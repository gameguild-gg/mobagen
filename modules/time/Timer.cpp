//
// Created by Alexandre Tolstenko Nogueira on 2024.06.12.
//

#include "TimeDefs.h"
#include "Time.h"
#include "Timer.h"
#include <stdexcept>

namespace MoBaGEn::TimeManager {
  Timer::Timer(bool startOnCreation) {
    if (startOnCreation) {
      Start();
    }
  }

  void Timer::Start() {
    if (!m_running) {
      m_startTime = Time::Now();
      m_running = true;
    } else {
      throw std::runtime_error("Timer already running");
    }
  }

  Duration Timer::Stop() {
    if (m_running) {
      m_elapsedTime += Time::Now() - m_startTime;
      m_running = false;
    }
    return m_elapsedTime;
  }

  void Timer::Reset() {
    m_elapsedTime = Duration(0);
    Start();
  }

}  // namespace MoBaGEn::TimeManager