//
// Created by Alexandre Tolstenko Nogueira on 2024.06.11.
//

#include <chrono>
#include "TimeDefs.h"
#include "Time.h"
namespace MoBaGEn::TimeManager {
  Duration Time::GetElapsedTime() { return Now() - m_startTime; }

  TimePoint Time::Now() { return std::chrono::high_resolution_clock::now(); }
}