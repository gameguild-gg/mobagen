#pragma once
// ============================================================================
// EventBus — asynchronous, by-type, decoupled messaging.
// ============================================================================
// Producers `post<E>()`; consumers `subscribe<E>()`. Events are QUEUED and
// delivered only when `process()` runs (e.g. at a frame-phase boundary), so
// producers and consumers are decoupled in time. Dispatch is by a per-type
// channel (compile-time-assigned id) — no string routing. Coarse events only
// (VolumeLoaded, SceneChanged, FrameCompleted — never RayHit/per-frame-per-entity).

// subscribe/unsubscribe/process are owner-thread control-plane operations.
// post is the worker-safe producer operation and serializes only per channel.
#include "threading/thread_bound.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace msg {

  namespace detail {
    inline std::size_t next_event_id() {
      static std::atomic_size_t next{0};
      return next.fetch_add(1, std::memory_order_relaxed);
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
      thread_bound_.require_owner_thread();
      const Id id = next_sub_++;
      channel<E>().subs.push_back({id, std::move(h)});
      return id;
    }

    template <class E> void unsubscribe(Id id) {
      thread_bound_.require_owner_thread();
      auto& subs = channel<E>().subs;
      for (auto it = subs.begin(); it != subs.end(); ++it)
        if (it->first == id) {
          subs.erase(it);
          return;
        }
    }

    template <class E> void post(E e) { channel<E>().post(std::move(e)); }

    void process() {
      thread_bound_.require_owner_thread();
      std::size_t channel_count = 0;
      {
        std::lock_guard<std::mutex> lock(channels_m_);
        channel_count = channels_.size();
      }
      for (std::size_t index = 0; index < channel_count; ++index) {
        IChannel* current = nullptr;
        {
          std::lock_guard<std::mutex> lock(channels_m_);
          current = channels_[index].get();
        }
        if (current != nullptr) current->drain();
      }
    }  // drain all types present when processing began
    template <class E> void process() {
      thread_bound_.require_owner_thread();
      channel<E>().drain();
    }  // drain one type

  private:
    struct IChannel {
      virtual ~IChannel() = default;
      virtual void drain() = 0;
    };

    template <class E> struct Channel : IChannel {
      std::vector<std::pair<Id, Handler<E>>> subs;
      std::vector<E> queue;
      std::mutex queue_m;

      void post(E event) {
        std::lock_guard<std::mutex> lock(queue_m);
        queue.push_back(std::move(event));
      }
      void drain() override {
        std::vector<E> q;
        {
          std::lock_guard<std::mutex> lock(queue_m);
          q.swap(queue);
        }
        for (auto& e : q)
          for (auto& s : subs) s.second(e);
      }
    };

    template <class E> Channel<E>& channel() {
      std::lock_guard<std::mutex> lock(channels_m_);
      const std::size_t id = detail::event_id<E>();
      if (id >= channels_.size()) channels_.resize(id + 1);
      if (!channels_[id]) channels_[id] = std::make_unique<Channel<E>>();
      return static_cast<Channel<E>&>(*channels_[id]);
    }

    threading::ThreadBound thread_bound_;
    std::mutex channels_m_;                            // protects channel registry creation
    std::vector<std::unique_ptr<IChannel>> channels_;  // by event id; channels never move
    Id next_sub_ = 1;
  };

}  // namespace msg
