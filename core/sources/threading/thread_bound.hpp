#pragma once
// Construction-thread ownership for control-plane APIs. The success path is a
// thread-id comparison only: no allocation, dispatch, registry, or string lookup.

#include <stdexcept>
#include <thread>

namespace threading {

  namespace detail {
    inline const std::thread::id& current_thread_id() noexcept {
      // Cache the platform lookup once per thread. The numeric id still compares
      // correctly when this inline header is instantiated in another module.
      static thread_local const std::thread::id id = std::this_thread::get_id();
      return id;
    }
  }  // namespace detail

  class ThreadBound {
  public:
    ThreadBound() noexcept : owner_(detail::current_thread_id()) {}

    [[nodiscard]] bool on_owner_thread() const noexcept { return owner_ == detail::current_thread_id(); }

    void require_owner_thread() const {
      if (!on_owner_thread()) {
        throw std::logic_error("operation must run on the object's construction thread");
      }
    }

  private:
    std::thread::id owner_;
  };

}  // namespace threading
