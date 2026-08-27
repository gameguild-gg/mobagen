#pragma once
// ============================================================================
// reactive — Angular-style reactive state: Signal / Computed / Effect.
// ============================================================================
// Auto dependency tracking: while an Effect (or a Computed's updater) runs, any
// Signal it READS records that consumer as a dependent. Writing a Signal re-runs
// its dependents. So derived state and side-effects update themselves.
//
//   Signal<float> center{40}, width{400};
//   Computed<float> key([&]{ return center.get()*1000 + width.get(); });
//   Effect upload([&]{ gpu_upload(key.get()); });   // re-runs when key changes
//   center.set(20);                                  // -> key recomputes -> upload
//
// CONTROL PLANE ONLY (editor params/UI) — never per-voxel/ray (ENGINE_ARCHITECTURE §2).
//
// This is a simple EAGER push model (set -> synchronously re-run dependents),
// enough for editor state. Enhancements left for later: lazy/glitch-free pull
// (Angular's model), dynamic-dependency cleanup, and unsubscribe-on-destroy.
// LIFETIME RULE for now: an Effect/Computed must outlive every Signal it reads
// (no set() may fire after a reader is destroyed).

#include <functional>
#include <unordered_set>
#include <utility>

namespace reactive {

  // A consumer (Effect, or a Computed's internal updater) that can re-run.
  struct Consumer {
    std::function<void()> run;
  };

  // The consumer currently executing — set while an Effect/Computed body runs.
  inline thread_local Consumer* g_active = nullptr;

  template <class T> class Signal {
  public:
    explicit Signal(T value) : value_(std::move(value)) {}

    const T& get() {
      if (g_active) subs_.insert(g_active);  // auto-track this reader
      return value_;
    }

    void set(T value) {
      if (value == value_) return;  // unchanged -> no propagation
      value_ = std::move(value);
      auto subs = subs_;  // copy: a run may re-subscribe
      for (auto* c : subs)
        if (c && c->run) c->run();
    }

    void update(const std::function<T(const T&)>& f) { set(f(value_)); }

  private:
    T value_;
    std::unordered_set<Consumer*> subs_;
  };

  class Effect {
  public:
    explicit Effect(std::function<void()> body) : body_(std::move(body)) {
      node_.run = [this] { run_tracked(); };
      run_tracked();  // run once, capturing dependencies
    }

  private:
    void run_tracked() {
      Consumer* prev = g_active;
      g_active = &node_;
      body_();
      g_active = prev;
    }

    std::function<void()> body_;
    Consumer node_;
  };

  // A derived value: a Signal kept up to date by an internal Effect.
  template <class T> class Computed {
  public:
    explicit Computed(std::function<T()> f)
        : value_(f()),                                 // initial (untracked)
          updater_([this, f] { value_.set(f()); }) {}  // recompute + push on change

    const T& get() { return value_.get(); }  // tracks the reader

  private:
    Signal<T> value_;
    Effect updater_;
  };

}  // namespace reactive
