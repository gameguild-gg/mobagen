#ifndef MOBAGEN_NOTIMPLEMENTED_H
#define MOBAGEN_NOTIMPLEMENTED_H

#include <stdexcept>

// Todo: use <stacktrace> when it's available
class NotImplemented : public std::logic_error {
public:
  NotImplemented() : std::logic_error("Not yet implemented"){};
};
#endif  // MOBAGEN_NOTIMPLEMENTED_H
