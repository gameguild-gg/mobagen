#pragma once
// ============================================================================
// Notifier<Args...> — synchronous, per-instance, multicast callback.
// ============================================================================
// The Qt/Godot "signal" idea, renamed so it never collides with reactive::Signal.
// `connect` listeners; `emit` calls them ALL right now, in connection order.
// Use for local, immediate, instance-specific reactions where the emitter is
// known. For decoupled / async / by-type notifications, use EventBus instead.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace msg {

  template <class... Args> class Notifier {
  public:
    using Slot = std::function<void(Args...)>;
    using Id = std::uint64_t;

    Id connect(Slot fn) {
      const Id id = next_++;
      slots_.push_back({id, std::move(fn)});
      return id;
    }

    void disconnect(Id id) {
      for (auto it = slots_.begin(); it != slots_.end(); ++it)
        if (it->id == id) {
          slots_.erase(it);
          return;
        }
    }

    void emit(Args... args) const {
      auto snapshot = slots_;  // re-entrancy safe: a slot may (dis)connect
      for (auto& s : snapshot) s.fn(args...);
    }

    std::size_t size() const { return slots_.size(); }

  private:
    struct Entry {
      Id id;
      Slot fn;
    };
    std::vector<Entry> slots_;
    Id next_ = 1;
  };

}  // namespace msg
