#pragma once
// ============================================================================
// net — the distributed seam.
// ============================================================================
// A Transport carries a SERIALIZED work item to a remote "node" and brings the
// result back. Only the transport changes between in-process (LoopbackNode here),
// a TCP socket, a WebSocket, or WebRTC — the seam is identical. This is the
// JobSystem's remote backend in miniature: serialize a job + its data, send it,
// the node executes, the serialized result returns. The World stays the source of
// truth; remote jobs operate on serialized component slices.

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace net {

  using Blob = std::vector<std::byte>;

  struct ITransport {
    virtual ~ITransport() = default;
    virtual void send(Blob request) = 0;  // client -> node
    virtual bool poll(Blob& out) = 0;     // client <- node (non-blocking; false if none yet)
  };

  // An in-process "node": a background thread runs `handler` on each request and
  // queues the response. Stands in for a remote process/server — swap it for a
  // socket transport and nothing else changes.
  class LoopbackNode : public ITransport {
  public:
    using Handler = std::function<Blob(const Blob&)>;

    explicit LoopbackNode(Handler handler) : handler_(std::move(handler)) {
      worker_ = std::thread([this] { run(); });
    }
    ~LoopbackNode() override {
      {
        std::lock_guard<std::mutex> lk(m_);
        stop_ = true;
      }
      cv_.notify_all();
      if (worker_.joinable()) worker_.join();
    }

    void send(Blob request) override {
      {
        std::lock_guard<std::mutex> lk(m_);
        req_.push_back(std::move(request));
      }
      cv_.notify_one();
    }
    bool poll(Blob& out) override {
      std::lock_guard<std::mutex> lk(resp_m_);
      if (resp_.empty()) return false;
      out = std::move(resp_.front());
      resp_.pop_front();
      return true;
    }

  private:
    void run() {
      for (;;) {
        Blob req;
        {
          std::unique_lock<std::mutex> lk(m_);
          cv_.wait(lk, [this] { return stop_ || !req_.empty(); });
          if (stop_ && req_.empty()) return;
          req = std::move(req_.front());
          req_.pop_front();
        }
        Blob resp = handler_(req);  // "remote" execution
        {
          std::lock_guard<std::mutex> lk(resp_m_);
          resp_.push_back(std::move(resp));
        }
      }
    }

    Handler handler_;
    std::thread worker_;
    std::mutex m_, resp_m_;
    std::condition_variable cv_;
    std::deque<Blob> req_, resp_;
    bool stop_ = false;
  };

  // Minimal POD (de)serialization for building request/response blobs.
  template <class T> void put(Blob& b, const T& v) {
    const auto* p = reinterpret_cast<const std::byte*>(&v);
    b.insert(b.end(), p, p + sizeof(T));
  }
  template <class T> T get(const Blob& b, std::size_t& off) {
    T v;
    std::memcpy(&v, b.data() + off, sizeof(T));
    off += sizeof(T);
    return v;
  }

}  // namespace net
