// net demo: offload a computation to a remote "node" through the transport seam.
// The client serializes {lo, hi}, sends it; the node sums [lo, hi) and returns
// the serialized result; the client deserializes and verifies it against a local
// computation. Swap LoopbackNode for a socket/WebSocket and this is real distribution.

#include "transport.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>

using namespace net;

// Runs on the "node": deserialize a range, return its sum, serialized.
static Blob sum_handler(const Blob& req) {
  std::size_t off = 0;
  const auto lo = get<std::uint64_t>(req, off);
  const auto hi = get<std::uint64_t>(req, off);
  std::uint64_t acc = 0;
  for (auto i = lo; i < hi; ++i) acc += i;
  Blob resp;
  put(resp, acc);
  return resp;
}

int main() {
  LoopbackNode node(sum_handler);  // a "remote" node on its own thread

  const std::uint64_t lo = 0, hi = 10'000'000;
  std::printf("== net seam: offload sum[%llu,%llu) to a node ==\n", static_cast<unsigned long long>(lo), static_cast<unsigned long long>(hi));

  Blob req;  // serialize the work
  put(req, lo);
  put(req, hi);
  node.send(std::move(req));  // -> transport -> node

  Blob resp;  // await the serialized result
  while (!node.poll(resp)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  std::size_t off = 0;
  const auto remote = get<std::uint64_t>(resp, off);

  std::uint64_t local = 0;
  for (auto i = lo; i < hi; ++i) local += i;

  std::printf("remote=%llu  local=%llu  [%s]\n", static_cast<unsigned long long>(remote), static_cast<unsigned long long>(local),
              remote == local ? "OK" : "MISMATCH");
  return 0;
}
