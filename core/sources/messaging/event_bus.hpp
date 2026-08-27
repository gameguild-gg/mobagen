#pragma once
// ============================================================================
// EventBus — asynchronous, by-type, decoupled messaging.
// ============================================================================
// Producers `post<E>()`; consumers `subscribe<E>()`. Events are QUEUED and
// delivered only when `process()` runs (e.g. at a frame-phase boundary), so
// producers and consumers are decoupled in time. Dispatch is by a per-type
// channel (compile-time-assigned id) — no string routing. Coarse events only
// (VolumeLoaded, SceneChanged, FrameCompleted — never RayHit/per-frame-per-entity).

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace msg {

  namespace detail {
    inline std::size_t next_event_id() {
      static std::size_t c = 0;
      return c++;
    }
    template <class E> std::size_t event_id() {
      static const std::size_t id = next_event_id();
      return id;
    }
  }  // namespace detail

  class EventBus {
  public:
    template <class E> using Handler = std::function<void(const E&)>;
    using Id = std::uint64_t;

    template <class E> Id subscribe(Handler<E> h) {
      const Id id = next_sub_++;
      channel<E>().subs.push_back({id, std::move(h)});
      return id;
    }

    template <class E> void unsubscribe(Id id) {
      auto& subs = channel<E>().subs;
      for (auto it = subs.begin(); it != subs.end(); ++it)
        if (it->first == id) {
          subs.erase(it);
          return;
        }
    }

    template <class E> void post(E e) { channel<E>().queue.push_back(std::move(e)); }

    void process() {
      for (auto& c : channels_)
        if (c) c->drain();
    }                                                            // drain all types
    template <class E> void process() { channel<E>().drain(); }  // drain one type

  private:
    struct IChannel {
      virtual ~IChannel() = default;
      virtual void drain() = 0;
    };

    template <class E> struct Channel : IChannel {
      std::vector<std::pair<Id, Handler<E>>> subs;
      std::vector<E> queue;
      void drain() override {
        auto q = std::move(queue);
        queue.clear();
        for (auto& e : q)
          for (auto& s : subs) s.second(e);
      }
    };

    template <class E> Channel<E>& channel() {
      const std::size_t id = detail::event_id<E>();
      if (id >= channels_.size()) channels_.resize(id + 1);
      if (!channels_[id]) channels_[id] = std::make_unique<Channel<E>>();
      return static_cast<Channel<E>&>(*channels_[id]);
    }

    std::vector<std::unique_ptr<IChannel>> channels_;  // by event id
    Id next_sub_ = 1;
  };

}  // namespace msg
